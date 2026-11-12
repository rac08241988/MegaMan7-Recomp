#include "recomp_updater.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#endif

static void set_error(RecompUpdaterResult *out, const char *msg) {
    if (!out) return;
    out->status = RECOMP_UPDATER_ERROR;
    snprintf(out->error, sizeof(out->error), "%s", msg ? msg : "unknown error");
}

static void strip_v(const char *in, char *out, size_t cap) {
    if (!in) in = "";
    if (in[0] == 'v' || in[0] == 'V') in++;
    snprintf(out, cap, "%s", in);
}

int recomp_updater_version_cmp(const char *a, const char *b) {
    char sa[64], sb[64];
    strip_v(a, sa, sizeof(sa));
    strip_v(b, sb, sizeof(sb));
    const char *pa = sa, *pb = sb;
    while (*pa || *pb) {
        long va = 0, vb = 0;
        while (*pa && *pa != '.') {
            if (isdigit((unsigned char)*pa)) va = va * 10 + (*pa - '0');
            pa++;
        }
        while (*pb && *pb != '.') {
            if (isdigit((unsigned char)*pb)) vb = vb * 10 + (*pb - '0');
            pb++;
        }
        if (va != vb) return va < vb ? -1 : 1;
        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
    }
    return 0;
}

static int json_string_field(const char *json, const char *key, char *out, size_t cap) {
    /* Look for "key" then the next JSON string value. */
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        p += strlen(pat);
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')) p++;
        if (*p != '"') continue;
        p++;
        size_t n = 0;
        while (*p && *p != '"' && n + 1 < cap) {
            if (*p == '\\' && p[1]) {
                p++;
                out[n++] = *p++;
                continue;
            }
            out[n++] = *p++;
        }
        out[n] = 0;
        return 0;
    }
    return -1;
}

int recomp_updater_parse_release_json(const char *json, RecompUpdaterResult *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->status = RECOMP_UPDATER_ERROR;
    if (!json || !json[0]) {
        set_error(out, "Empty response from GitHub");
        return -1;
    }
    /* A 404 or error payload has "message" and no tag_name. */
    if (json_string_field(json, "tag_name", out->tag, sizeof(out->tag)) != 0) {
        char msg[200];
        if (json_string_field(json, "message", msg, sizeof(msg)) == 0)
            snprintf(out->error, sizeof(out->error), "GitHub: %s", msg);
        else
            snprintf(out->error, sizeof(out->error), "No release tag in GitHub response");
        return -1;
    }
    strip_v(out->tag, out->latest, sizeof(out->latest));
    /* Prefer the release html_url (contains /releases/tag/). */
    if (json_string_field(json, "html_url", out->release_url, sizeof(out->release_url)) != 0)
        out->release_url[0] = 0;
    return 0;
}

#ifdef _WIN32
static int http_get_https(const wchar_t *host, const wchar_t *path, char **body_out, const char **err) {
    *body_out = NULL;
    if (err) *err = NULL;
    HINTERNET ses = WinHttpOpen(L"Recomp-Updater/1.0",
                                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { if (err) *err = "WinHttpOpen failed"; return -1; }
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 12000);
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { WinHttpCloseHandle(ses); if (err) *err = "WinHttpConnect failed"; return -1; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) {
        WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "WinHttpOpenRequest failed";
        return -1;
    }
    const wchar_t *hdrs = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    BOOL ok = WinHttpSendRequest(req, hdrs, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, NULL);
    if (!ok) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "GitHub request failed (network)";
        return -1;
    }
    size_t cap = 0, len = 0;
    char *buf = NULL;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        if (len + avail + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 8192;
            while (ncap < len + avail + 1) ncap *= 2;
            char *nbuf = (char *)realloc(buf, ncap);
            if (!nbuf) { free(buf); buf = NULL; break; }
            buf = nbuf; cap = ncap;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, buf + len, avail, &got) || got == 0) break;
        len += got;
        buf[len] = 0;
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    if (!buf) { if (err) *err = "Empty body from GitHub"; return -1; }
    *body_out = buf;
    return 0;
}
#endif

RecompUpdaterStatus recomp_updater_check(const char *owner, const char *repo,
                                         const char *current_version,
                                         RecompUpdaterResult *out) {
    RecompUpdaterResult local;
    if (!out) out = &local;
    memset(out, 0, sizeof(*out));
    out->status = RECOMP_UPDATER_ERROR;

    const char *env_ver = getenv("RECOMP_UPDATER_CURRENT_VERSION");
    if (env_ver && env_ver[0]) current_version = env_ver;
    if (!current_version || !current_version[0]) current_version = "0.0.0";
    snprintf(out->current, sizeof(out->current), "%s", current_version);

    if (!owner || !owner[0] || !repo || !repo[0]) {
        set_error(out, "Missing GitHub owner/repo");
        return out->status;
    }

#ifndef _WIN32
    /* The shipping build is Windows. On a Linux host this module still
     * compiles so version/JSON unit tests can run; the live HTTP path is
     * Windows-only (WinHTTP). */
    set_error(out, "Update check is only implemented on Windows");
    return out->status;
#else
    wchar_t path[256];
    swprintf(path, 256, L"/repos/%S/%S/releases/latest", owner, repo);
    char *body = NULL;
    const char *err = NULL;
    if (http_get_https(L"api.github.com", path, &body, &err) != 0) {
        set_error(out, err ? err : "GitHub request failed");
        return out->status;
    }
    RecompUpdaterResult parsed;
    int pr = recomp_updater_parse_release_json(body, &parsed);
    free(body);
    snprintf(out->current, sizeof(out->current), "%s", current_version);
    if (pr != 0) {
        out->status = RECOMP_UPDATER_ERROR;
        snprintf(out->error, sizeof(out->error), "%s", parsed.error);
        return out->status;
    }
    snprintf(out->tag, sizeof(out->tag), "%s", parsed.tag);
    snprintf(out->latest, sizeof(out->latest), "%s", parsed.latest);
    snprintf(out->release_url, sizeof(out->release_url), "%s", parsed.release_url);
    if (!out->release_url[0]) {
        snprintf(out->release_url, sizeof(out->release_url),
                 "https://github.com/%s/%s/releases/latest", owner, repo);
    }
    if (recomp_updater_version_cmp(current_version, out->latest) >= 0)
        out->status = RECOMP_UPDATER_UP_TO_DATE;
    else
        out->status = RECOMP_UPDATER_UPDATE_AVAILABLE;
    out->error[0] = 0;
    return out->status;
#endif
}

int recomp_updater_open_url(const char *url) {
    if (!url || !url[0]) return -1;
#ifdef _WIN32
    HINSTANCE r = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)r > 32) ? 0 : -1;
#else
    char cmd[640];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 || true", url);
    return system(cmd) == 0 ? 0 : -1;
#endif
}

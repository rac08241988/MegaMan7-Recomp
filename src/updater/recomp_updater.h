/* Reusable "Check for Updates" helper for SNES Recomp ports.
 *
 * Queries the GitHub Releases API for a repo's latest release, compares it
 * with the build's version, and can open the release page in the default
 * browser. No ROM data, no auto-download: the player chooses whether to
 * open the page.
 *
 * This is the canonical copy. Game trees vendor a copy under src/updater/
 * so a player's first-run rebuild does not depend on this machine path.
 */
#ifndef RECOMP_UPDATER_H
#define RECOMP_UPDATER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RecompUpdaterStatus {
    RECOMP_UPDATER_UP_TO_DATE = 0,
    RECOMP_UPDATER_UPDATE_AVAILABLE = 1,
    RECOMP_UPDATER_ERROR = 2
} RecompUpdaterStatus;

typedef struct RecompUpdaterResult {
    RecompUpdaterStatus status;
    char current[32];
    char latest[32];      /* tag_name with a leading v/V stripped */
    char tag[32];         /* raw tag_name from the API */
    char release_url[512];
    char error[256];
} RecompUpdaterResult;

/* Parse GitHub /releases/latest JSON into tag + html_url. Returns 0 on
 * success. Used by recomp_updater_check and by the unit test. */
int recomp_updater_parse_release_json(const char *json, RecompUpdaterResult *out);

/* Compare dotted numeric versions (1.0.0, v1.2, 1.10.0). A leading v/V is
 * ignored. Returns <0 if a<b, 0 if equal, >0 if a>b. */
int recomp_updater_version_cmp(const char *a, const char *b);

/* Query https://api.github.com/repos/<owner>/<repo>/releases/latest and
 * compare with current_version. current_version may be overridden at
 * runtime by the RECOMP_UPDATER_CURRENT_VERSION environment variable
 * (used to test the "update available" path with a fake lower version).
 * out must be non-NULL. Returns out->status. */
RecompUpdaterStatus recomp_updater_check(const char *owner, const char *repo,
                                         const char *current_version,
                                         RecompUpdaterResult *out);

/* Open a URL in the default browser. Returns 0 on success. */
int recomp_updater_open_url(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* RECOMP_UPDATER_H */

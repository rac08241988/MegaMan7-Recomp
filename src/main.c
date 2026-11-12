/*
 * Mega Man 7 — desktop host shim.
 *
 * Identity and hooks, nothing else. The host itself is the framework's
 * (snesrecomp/runner/src/desktop/host_main.h): the pre-boot launcher, ROM
 * resolution and digest checks, config.ini and keybinds.ini, the window and
 * the SDL / OpenGL presenters, audio, gamepads, the in-game save-state
 * browser and rewind filmstrip, the OSD, the pacing clock, crash handlers and
 * the post-mortem report, mod packages, Generate & rebuild, and the netplay
 * barrier when the project is built with it. A fix there reaches this
 * project on a submodule pull; nothing in this file needs to change for it.
 *
 * This file used to BE the host -- a ~700-line copy made at scaffold time --
 * so every project scaffolded before a fix kept the bug. Two ports of the
 * same game, a week apart, had different hosts.
 *
 * Nothing here ever ships a ROM. The player supplies one; the host makes
 * that easy (launcher, then positional argument, then a copy beside the
 * executable, then the rom.cfg cache, then a file picker) and checks what it
 * is handed against the digests in rom_identity.txt.
 *
 * Grow this file only with what is specific to THIS title: a custom
 * presenter (prepare_frame / draw_frame), an SPC player, a pacing rule
 * (keep_pacing_debt), a Mods provider. See SnesDesktopHostGame for the
 * complete list of hooks and what each is for.
 */

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include "host_main.h"
#include "game_rtl.h"
#include "dc_display.h"
#include "dc_widescreen.h"
#include "dc_options.h"   /* F12 options menu: shader presets, 120 Hz motion */
#include "dc_interp.h"    /* 120 Hz motion: sub-frame re-raster */
#include "desktop/config.h"
#include "snesrecomp_rom_identity.h"  /* generated from rom_identity.txt */
#include "host_paths.h"
#include "recomp_launcher.h"

#ifndef __ANDROID__
#define SDL_MAIN_HANDLED 1
#endif
#include "desktop/sdl_compat.h"
#ifdef __ANDROID__
/* SDLActivity loads libmain.so and dlsyms its SDL_main. */
#define main SDL_main
#endif

#ifndef SNES_GAME_VERSION
#define SNES_GAME_VERSION "dev"
#endif

/* App icon: original SNES front box art, letterboxed, staged as icon.png
 * and icon.bmp beside the exe. The Windows .ico is also embedded via
 * src/win32/app.rc so Explorer and the taskbar show it on both the setup
 * host and the playable rebuild. */
static char g_mm7_icon_path[1024];

static int Mm7FileExists(const char *path) {
#ifdef _WIN32
    return path && path[0] && _access(path, 0) == 0;
#else
    return path && path[0] && access(path, F_OK) == 0;
#endif
}

static void Mm7ResolveIconPath(void) {
    char dir[800];
    if (g_mm7_icon_path[0]) return;
    dir[0] = 0;
#ifdef _WIN32
    {
        wchar_t wpath[MAX_PATH];
        char utf8[MAX_PATH * 4];
        DWORD n = GetModuleFileNameW(NULL, wpath, MAX_PATH);
        wchar_t *slash;
        if (n == 0 || n >= MAX_PATH) return;
        slash = wcsrchr(wpath, L'\\');
        if (!slash) return;
        slash[1] = L'\0';
        if (WideCharToMultiByte(CP_UTF8, 0, wpath, -1, utf8, (int)sizeof(utf8),
                                NULL, NULL) <= 0)
            return;
        snprintf(dir, sizeof(dir), "%s", utf8);
    }
#else
    {
        char link[800];
        ssize_t n = readlink("/proc/self/exe", link, sizeof(link) - 1);
        char *slash;
        if (n <= 0) return;
        link[n] = '\0';
        slash = strrchr(link, '/');
        if (!slash) return;
        slash[1] = '\0';
        snprintf(dir, sizeof(dir), "%s", link);
    }
#endif
    /* Prefer the PNG (launcher stb loader); fall back to BMP / icon/. */
    snprintf(g_mm7_icon_path, sizeof(g_mm7_icon_path), "%sicon.png", dir);
    if (Mm7FileExists(g_mm7_icon_path)) return;
    snprintf(g_mm7_icon_path, sizeof(g_mm7_icon_path), "%sicon.bmp", dir);
    if (Mm7FileExists(g_mm7_icon_path)) return;
    snprintf(g_mm7_icon_path, sizeof(g_mm7_icon_path), "%sicon/icon.png", dir);
    if (Mm7FileExists(g_mm7_icon_path)) return;
    snprintf(g_mm7_icon_path, sizeof(g_mm7_icon_path), "%sassets/icon.png", dir);
    if (Mm7FileExists(g_mm7_icon_path)) return;
    g_mm7_icon_path[0] = 0;
}

#ifdef RECOMP_LAUNCHER
static void Mm7ConfigureLauncher(RecompLauncherCGameInfo *info) {
    Mm7ResolveIconPath();
    if (g_mm7_icon_path[0]) info->window_icon_path = g_mm7_icon_path;
}
#endif

static void Mm7ApplySdlWindowIcon(void) {
    static int applied = 0;
    SDL_Surface *surf = NULL;
    char bmp[1024];
#if SNESRECOMP_SDL3
    int n = 0;
    SDL_Window **wins = NULL;
#endif
    if (applied) return;
    applied = 1;
    Mm7ResolveIconPath();
    /* Load a BMP for SDL_SetWindowIcon (no SDL_image required). */
    bmp[0] = 0;
    if (g_mm7_icon_path[0]) {
        size_t len = strlen(g_mm7_icon_path);
        if (len > 4) {
            snprintf(bmp, sizeof(bmp), "%s", g_mm7_icon_path);
            memcpy(bmp + len - 4, ".bmp", 5);
            if (!Mm7FileExists(bmp)) {
                char *slash = strrchr(bmp, '/');
#ifdef _WIN32
                char *bslash = strrchr(bmp, '\\');
                if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
                if (slash) snprintf(slash + 1, sizeof(bmp) - (size_t)(slash + 1 - bmp), "icon.bmp");
            }
        }
    }
    if (!Mm7FileExists(bmp)) bmp[0] = 0;
    if (bmp[0]) surf = SDL_LoadBMP(bmp);

#if SNESRECOMP_SDL3
    wins = SDL_GetWindows(&n);
    if (wins) {
        for (int i = 0; i < n; i++) {
            if (surf) SDL_SetWindowIcon(wins[i], surf);
#ifdef _WIN32
            {
                HWND hwnd = (HWND)SDL_GetPointerProperty(
                    SDL_GetWindowProperties(wins[i]),
                    SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
                if (hwnd) {
                    HICON icon = LoadIconW(GetModuleHandleW(NULL),
                                           MAKEINTRESOURCEW(1));
                    if (icon) {
                        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
                        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
                    }
                }
            }
#endif
        }
        SDL_free(wins);
    }
#else
    (void)surf;
#endif
    if (surf) {
#if SNESRECOMP_SDL3
        SDL_DestroySurface(surf);
#else
        SDL_FreeSurface(surf);
#endif
    }
}

static void DcPrepareFrame(int drawable_w, int drawable_h, int *w, int *h) {
    Mm7ApplySdlWindowIcon();
    SnesDisplayAspect aspect = SnesDisplayAspect_Clamp(g_config.display_aspect);
    *w = DcDisplay_IsWidescreenEnabled()
             ? DcDisplay_ComputeFrameWidth(g_dc_aspect, drawable_w,
                                           drawable_h, aspect)
             : 256;
    *h = DC_FRAME_HEIGHT;
}

static void DcBeginFrame(unsigned number) {
    DcOptions_OnSimFrame(number);
    DcWidescreenConfigureFrame();
}

static void DcEndFrame(const uint8_t *field, unsigned number) {
    (void)field;
    (void)number;
    DcWidescreenAfterRaster();
}

static int DcWindowWidth(int frame_width) {
    return DcDisplay_GetWindowBaseWidth(
        frame_width, SnesDisplayAspect_Clamp(g_config.display_aspect));
}

static void DcComputeViewport(int frame_w, int frame_h, int drawable_w,
                              int drawable_h, SnesDisplayViewport *viewport) {
    DcDisplay_ComputeViewport(
        frame_w, frame_h, drawable_w, drawable_h,
        SnesDisplayAspect_Clamp(g_config.display_aspect),
        g_config.ignore_aspect_ratio, false, viewport);
}

static const char *const kCdWidescreenLabels[] = {"Off", "16:9"};
static int g_cd_ws_seeded = -1;
static int DcLauncherWidescreenGet(void) {
    g_cd_ws_seeded = DcWidescreenSetting_Get();
    return g_cd_ws_seeded;
}
static void DcLauncherWidescreenSet(int index) {
    if (index != g_cd_ws_seeded) DcWidescreenSetting_Set(index);
    g_cd_ws_seeded = index;
}


static const SnesDesktopHostGame kGameHost = {
    .display_name        = "Mega Man 7",
    .window_title        = "Mega Man 7",
    .region              = SNESRECOMP_ROM_REGION,
    .rom_file            = SNESRECOMP_ROM_FILE,
    .expected_sha256_hex = SNESRECOMP_ROM_EXPECTED_SHA256,
    .expected_crc32_hex  = SNESRECOMP_ROM_EXPECTED_CRC32,
    .game_id             = SNESRECOMP_ROM_GAME_ID,
    .build_version       = SNES_GAME_VERSION,
    .game_info           = &kGameInfo,
    .num_players         = 1,
    /* Opt-in 16:9 widescreen (default off = 4:3 with side art). */
    .widescreen_supported = 1,
    .native_widescreen   = 1,
    .prepare_frame       = DcPrepareFrame,
    .begin_sim_frame     = DcBeginFrame,
    .end_sim_frame       = DcEndFrame,
    .window_base_width   = DcWindowWidth,
    .window_base_height  = DcDisplay_GetWindowBaseHeight,
    .compute_viewport    = DcComputeViewport,
    /* Mega Man 7 (USA) has no SRAM (header cart type $00,
     * SRAM size $00). Leave the SRAM path NULL so the launcher does not
     * offer a SAVES panel for storage the cartridge never had. */
    .sram_path           = NULL,
    /* In-game Options menu (F12 or Escape / gamepad Guide): the shader preset picker
     * and the 120 Hz motion mode, persisted in dc_options.ini. */
    .after_config        = DcOptions_AfterConfig,
    .default_config_ini  = kDcDefaultConfigIni,
    .on_key              = DcOptions_OnKey,
    .on_gamepad_button   = DcOptions_OnGamepadButton,
    .modal_active        = DcOptions_ModalActive,
    .draw_modal          = DcOptions_DrawModal,
    .draw_modal_layer    = DcOptions_DrawModalLayer,
    /* 120 Hz motion: decouple presentation on 100 Hz+ displays and compose
     * in-between frames (game logic stays at 60 Hz). */
    .presentation_hz     = DcInterp_PresentationHz,
    .draw_frame          = DcInterp_DrawFrame,
    /* Ctrl+L / Select+L3 and the in-game Options' "Keybinds & controller"
     * row open the launcher mid-game; renderer / audio-rate / mod changes
     * restart and resume at the same moment. */
    .in_game_launcher    = 1,
    .aspect_labels        = kCdWidescreenLabels,
    .num_aspect_labels    = 2,
    .aspect_setting_label = "Widescreen",
    .aspect_setting_help  =
        "Off keeps the 4:3 picture with side art. 16:9 shows extra stage "
        "beside the original view; scenes whose edges would glitch stay on "
        "side art. Capped at 1080p and 60 fps.",
    .aspect_get           = DcLauncherWidescreenGet,
    .aspect_set           = DcLauncherWidescreenSet,
    .configure_launcher   = Mm7ConfigureLauncher,
};

#ifndef __ANDROID__
#undef main   /* desktop: keep plain main() even if SDL_main.h remapped it */
#endif
#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#include <io.h>
/* The playable Windows build uses the GUI subsystem (no console window).
 * Logs: a terminal launch attaches to that console; --console opens a new
 * one; otherwise stdout/stderr go to MegaMan7.log beside the exe. */
static void dc_win_redirect_logs(int argc, char **argv)
{
    if (GetStdHandle(STD_ERROR_HANDLE)) return;   /* already redirected */
    int want_console = 0;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--console") == 0) want_console = 1;
    if (AttachConsole(ATTACH_PARENT_PROCESS) || (want_console && AllocConsole())) {
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
        (void)freopen("CONIN$", "r", stdin);
        return;
    }
    wchar_t path[MAX_PATH + 32];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy(slash + 1, L"MegaMan7.log");
    /* One file: stdout truncates it, then both streams append unbuffered, so
     * each write lands at the current end and neither overwrites the other.
     * (A GUI app's stderr FILE has no descriptor, so _dup2 alone misses it.) */
    FILE *f = _wfreopen(path, L"w", stdout);
    if (f) f = _wfreopen(path, L"a", stdout);
    if (f) setvbuf(stdout, NULL, _IONBF, 0);
    if (_wfreopen(path, L"a", stderr)) setvbuf(stderr, NULL, _IONBF, 0);
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    dc_win_redirect_logs(argc, argv);
    {   /* --console is ours; keep it away from the framework's ROM/flag parser. */
        int j = 1;
        for (int i = 1; i < argc; i++)
            if (strcmp(argv[i], "--console") != 0) argv[j++] = argv[i];
        argv[j] = NULL;
        argc = j;
    }
#endif
    return snesrecomp_desktop_main(&kGameHost, argc, argv);
}

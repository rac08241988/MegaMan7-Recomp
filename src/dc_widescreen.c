/*
 * Mega Man 7 — widescreen scene policy.
 *
 * Adapted from the Demon's Crest host-side policy (dc_widescreen.c), but the
 * guest RAM/HUD/cull addresses in that title do not apply here. Opt-in 16:9:
 *   - Off (default): authentic 256-column 4:3 with side art / backdrop in
 *     any extra window space;
 *   - outside stage gameplay (logos, intro, title, passwords, menus,
 *     cutscenes) every BG layer is clamped, so the authentic picture sits
 *     centered with side art in the margins;
 *   - in stage gameplay BG1/BG2 render into the margins (the extra
 *     horizontal level) while BG3 stays clamped so a HUD or overlay cannot
 *     wrap. If a scene's edges would show wrap garbage, the gameplay gate
 *     keeps it clamped (side-art fallback).
 *
 * Fase 1/2 of docs/WIDESCREEN_PLAN.md (template notes, DC-specific HUD):
 *   - outside stage gameplay every BG layer is clamped;
 *   - in DC stage gameplay BG1/BG2 widened while BG3 status bar stayed native.
 *
 * Fase 3: the margins are served from the framework's world-keyed tilemap
 * store (snes/ws_shadow.c) instead of the streamed VRAM window. That store
 * is the "bigger framebuffer" this game needs: 4096x512 tiles, keyed by the
 * world camera, fed by the native viewport sweep AND by the game's own VRAM
 * uploads at write time (WsShadowOnVramWrite) — so content the game stages
 * ahead of the view is available to the margins the moment it is written.
 * Without it, a scroll shows the streamed window's stale columns in the
 * gutter until the game rewrites them.
 *
 * PpuSetExtraSpaceCentered is deliberately NOT used here: it renders the
 * native columns without writing the side margins at all (the caller would
 * have to clear them), so a reused host buffer would show the previous
 * scene's pixels there.
 */

#include "dc_widescreen.h"

#include <stdio.h>
#include <stdlib.h>

#include "desktop/config.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include "widescreen.h"

extern Ppu *g_ppu;

DcRenderAspect g_dc_aspect = DC_ASPECT_ADAPTIVE;

static bool g_cd_stage;          /* latched once a scrolling stage is seen */
static uint16_t g_cd_last_h = 0xFFFF;
static int g_cd_still_frames;

void DcWidescreenReset(void) {
    g_dc_aspect = DC_ASPECT_16_9;
    g_cd_stage = false;
    g_cd_last_h = 0xFFFF;
    g_cd_still_frames = 0;
}

/*
 * Gameplay gate for Mega Man 7 (not Demon's Crest $8D / $00FB).
 *
 * A scrolling Mode-0/1 playfield with BG1 and sprites is treated as a stage
 * once the camera has moved, or immediately if a playfield tilemap is 64
 * tiles wide (real extra columns exist in VRAM). Title, menus, passwords
 * and other static bounded screens stay clamped (side art). Returning to a
 * fully static, 32-wide, zero-scroll screen for ~1.5 s drops the latch so
 * the title after a reset does not stay widened.
 * SNESRECOMP_WS_GATE=1/0 forces the verdict for A/B captures.
 */
static bool DcStageGameplay(void) {
    static int forced = -2;
    if (forced == -2) {
        const char *env = getenv("SNESRECOMP_WS_GATE");
        forced = env && env[0] ? (env[0] != '0') : -1;
    }
    if (forced >= 0)
        return forced != 0;
    if (!g_ppu)
        return false;
    if (g_ppu->inidisp & 0x80)
        return false;
    if ((g_ppu->inidisp & 0x0F) < 0x08)
        return false;

    const int mode = g_ppu->bgmode & 7;
    const uint8_t main_on = g_ppu->screenEnabled[0];
    const bool bg1 = (main_on & 0x01) != 0;
    const bool obj = (main_on & 0x10) != 0;
    const bool wide0 = (g_ppu->bgXsc[0] & 1) != 0;
    const bool wide1 = (g_ppu->bgXsc[1] & 1) != 0;
    const uint16_t h0 = g_ppu->hScroll[0];
    const bool moved = (g_cd_last_h != 0xFFFF && h0 != g_cd_last_h);
    g_cd_last_h = h0;

    /* Mode 7 / high-color screens in this title are bounded art, not stages. */
    if (mode >= 3) {
        g_cd_stage = false;
        g_cd_still_frames = 0;
        return false;
    }
    if (!bg1 || !obj) {
        g_cd_stage = false;
        g_cd_still_frames = 0;
        return false;
    }

    if (wide0 || wide1 || moved)
        g_cd_stage = true;

    const bool zero_scroll = ((g_ppu->hScroll[0] | g_ppu->hScroll[1] |
                               g_ppu->vScroll[0] | g_ppu->vScroll[1]) & 0x3FF) == 0;
    if (g_cd_stage && zero_scroll && !wide0 && !wide1 && !moved) {
        if (++g_cd_still_frames > 90)
            g_cd_stage = false;
    } else {
        g_cd_still_frames = 0;
    }
    return g_cd_stage;
}

static void DcClearPolicies(void) {
    PpuSetWidescreenLayerClamp(g_ppu, 0);
    PpuSetWidescreenLayerMask(g_ppu, 0);
    PpuSetWidescreenBg3Widen(g_ppu, 0);
    PpuSetWsHudOamShiftRange(g_ppu, 0, 0);
    PpuSetWsHudOamShiftRange2(g_ppu, 0, 0);
    PpuSetWidescreenHudSplit(g_ppu, 0, 0, 0);
    PpuSetWidescreenLayerElasticBand(g_ppu, 2, 0, 0, NULL, 0);
    PpuSetIgnoreForcedBlank(g_ppu, false);
    PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL);
}

/* History retention window per side, in tiles. The widest margin is 213px
 * at 32:9 (~27 tiles); 32 covers the whole visible gutter plus the write
 * capture's ahead-of-view chunk staging. */
enum { DC_WS_KEEP_TILES = 32 };

/* Margin history switch, DEFAULT OFF (2026-10-18).
 *
 * The world-keyed store is fed by the native viewport sweep and by the
 * game's VRAM uploads, but ground-truth checks (a 256px camera shift with
 * the frame buffer as oracle) show its world keys do not match Demon's
 * Crest's actual tilemap window: the west (trailing) margin differed from
 * the same world region when it later became native in ~65% of pixels, the
 * east margin in ~78%. The margins then scroll at the right rate but show
 * the wrong section, which reads as a visibly different band. The
 * framework's model assumes a ring window anchored at the pixel scroll;
 * this game's streamer (and likely its per-line HDMA scrolls) does not
 * satisfy it. Until the window rule is derived, the raw streamed VRAM is
 * the better default: wrong content in the trailing gutter, but motion
 * identical to the rest of the screen.
 *
 * SNESRECOMP_WS_HISTORY=1 re-enables the history path for experiments. */
static bool DcHistoryEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("SNESRECOMP_WS_HISTORY");
        /* Default ON: Mega Man 7 has no HDMA camera to desync the
         * world-keyed store the way Demon's Crest did. SNESRECOMP_WS_HISTORY=0
         * turns it off. */
        enabled = !env || env[0] != '0';
    }
    return enabled != 0;
}

/*
 * Register one BG layer for world-keyed margin history. `world_x/y` is the
 * layer's own world origin (the game's 16-bit camera/parallax mirror); the
 * PPU's live scroll supplies the VRAM window phase (P1: phase from the PPU,
 * high bits from the game mirror). Must run every frame: the registration
 * only lives for the frame the sweep consumes.
 */
static void DcRegisterLayerHistory(int layer, uint16_t world_x,
                                   uint16_t world_y) {
    WsShadowSetWorld(layer, world_x, world_y);
    WsShadowSetScroll(layer, g_ppu->hScroll[layer], g_ppu->vScroll[layer]);
    /* World-row keys (no retainHistory): the viewport sweep and the
     * write-time capture then use the same Y key space. The retainHistory
     * variant keys Y by viewport row, which the write capture does not
     * (it uses world rows), so ahead-of-view uploads would never be read.
     * Demon's Crest has no elevator-style viewport rewrite to model. */
    WsShadowSetRetainHistory(layer, false);
}

/* Optional margin accounting: SNESRECOMP_WS_STATS=1 prints hit/miss and
 * fallback counters every 600 frames. A raw fallback is the case history
 * could not answer and the streamed VRAM window was used as-is. */
static void DcMaybePrintMarginStats(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("SNESRECOMP_WS_STATS");
        enabled = env && env[0] && env[0] != '0';
    }
    if (!enabled)
        return;
    static unsigned frames;
    if (++frames % 600)
        return;
    WsShadowMarginStat bg1, bg2;
    WsShadowGetMarginStats(0, &bg1);
    WsShadowGetMarginStats(1, &bg2);
    fprintf(stderr,
            "[ws] bg1 w=%llu/%llu e=%llu/%llu raw=%llu/%llu fold=%llu/%llu | "
            "bg2 w=%llu/%llu e=%llu/%llu raw=%llu/%llu fold=%llu/%llu\n",
            (unsigned long long)bg1.westHit, (unsigned long long)bg1.westMiss,
            (unsigned long long)bg1.eastHit, (unsigned long long)bg1.eastMiss,
            (unsigned long long)bg1.westRawFallback,
            (unsigned long long)bg1.eastRawFallback,
            (unsigned long long)bg1.westFold, (unsigned long long)bg1.eastFold,
            (unsigned long long)bg2.westHit, (unsigned long long)bg2.westMiss,
            (unsigned long long)bg2.eastHit, (unsigned long long)bg2.eastMiss,
            (unsigned long long)bg2.westRawFallback,
            (unsigned long long)bg2.eastRawFallback,
            (unsigned long long)bg2.westFold, (unsigned long long)bg2.eastFold);
}

/* ───────────────────────── Fase 6: status bar (BG3) ───────────────────────
 *
 * BG3 carries only the status bar: a left-anchored strip in the upper-left.
 * The VRAM map ($5800, 32x32) holds it on rows 1..5 (native y 8..47) at map
 * columns 3..29 (native x 24..239) — measured from a save-state dump; the rest
 * of the map is empty. In the widened field the bar would sit at native x 24,
 * leaving the whole left margin empty to its left.
 *
 * On the bar's scanlines BG3 is remapped as ONE RIGID segment shifted left by
 * the live margin (source [0,256) -> destination [-margin, 256-margin)): the
 * bar hugs the left edge of the widened field, and every column the remap does
 * not cover stays transparent, so the scenery shows through where the bar used
 * to sit. Below the band BG3 keeps its authentic position.
 *
 * The band is re-applied every frame (ppu_reset zeroes it). margin 0 is the
 * identity remap, i.e. the authentic 4:3 layout; SNESRECOMP_WS_HUD=0 disables
 * it. BG3 must be allowed into the widened line (PpuSetWidescreenBg3Widen) for
 * the band to be live: PpuWidescreenLayerExtra gates it, and the band's merge
 * then ignores the layer's own margin content and copies only the shifted
 * source span. That is also why BG3 is no longer clamped in gameplay — the
 * remap, not the clamp, keeps it in place.
 */
enum {
    DC_HUD_BAND_Y0 = 0,
    DC_HUD_BAND_Y1 = 48,
};

static int DcHudAnchorEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("SNESRECOMP_WS_HUD");
        /* Default off: Mega Man 7 does not use Demon's Crest's BG3 band. */
        enabled = env && env[0] ? (env[0] != '0') : 0;
    }
    return enabled;
}

static void DcAnchorStatusBar(void) {
    int m = g_ws_extra;
    if (!DcHudAnchorEnabled() || m <= 0) {
        PpuSetWidescreenBg3Widen(g_ppu, 0);
        PpuSetWidescreenLayerElasticBand(g_ppu, 2, 0, 0, NULL, 0);
        return;
    }
    if (m > kPpuExtraLeftRight)
        m = kPpuExtraLeftRight;
    /* The elastic band places the bar by remapping BG3's own columns, but the
     * band's merge only reaches the margins when BG3 is allowed to widen on
     * those scanlines. Widen BG3 ONLY on the band's rows: the dialogue box
     * lives on the same layer below it, and widening those rows wrapped its
     * options into the left margin ("NO" at x=0). */
    PpuSetWidescreenBg3WidenRange(g_ppu, 1, DC_HUD_BAND_Y1);
    PpuWsElasticSeg seg;
    seg.srcX0 = 0;
    seg.srcX1 = 256;
    seg.dstX0 = (int16_t)(-m);
    seg.dstX1 = (int16_t)(256 - m);
    PpuSetWidescreenLayerElasticBand(g_ppu, 2, DC_HUD_BAND_Y0, DC_HUD_BAND_Y1,
                                     &seg, 1);
}

/* Fase 7-ish (2026-10-19): the game covers its per-frame uploads with INIDISP
 * forced blank ($2100 = $80) and a "present" task unblanks later. This frame
 * model runs the CPU half before the raster walk, so the upload is never
 * visible; replaying the blank only darkens the first N visible lines, and N
 * varies with the frame's work (the top-of-screen band flicker the owner
 * reported). PpuSetIgnoreForcedBlank drops those writes from the render
 * replay while leaving brightness fades alone. Gameplay only: transitional
 * fields keep the authentic blank. SNESRECOMP_WS_NOBLANK=0 disables it. */
static int DcNoBlankEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("SNESRECOMP_WS_NOBLANK");
        enabled = env && env[0] ? (env[0] != '0') : 1;
    }
    return enabled;
}

void DcWidescreenConfigureFrame(void) {
    if (!g_ppu)
        return;

    if (!DcDisplay_IsWidescreenEnabled() || !DcDisplay_IsWidescreenActive()) {
        DcClearPolicies();
        /* The upload-cover policy (transient forced blank + HDMA disable
         * around the game's per-frame upload) is a frame-model correction,
         * not a display width: keep it active at 4:3 and in every scene. The
         * replay keeps a real field-long blank (no later unblank). */
        PpuSetIgnoreForcedBlank(g_ppu, DcNoBlankEnabled());
        return;
    }

    if (!DcStageGameplay()) {
        /* Bounded screens: clamp every BG layer (bits 0-2 = BG1-BG3) so the
         * authentic picture stays centered and the margins show only the
         * backdrop. The host's symmetric PpuSetExtraSpace still writes the
         * whole row, so no stale margin pixels survive a scene change. */
        PpuSetWidescreenLayerClamp(g_ppu, 0x07);
        PpuSetWidescreenLayerMask(g_ppu, 0);
        PpuSetWidescreenBg3Widen(g_ppu, 0);
        PpuSetWsHudOamShiftRange(g_ppu, 0, 0);
        PpuSetWsHudOamShiftRange2(g_ppu, 0, 0);
        PpuSetWidescreenLayerElasticBand(g_ppu, 2, 0, 0, NULL, 0);
        PpuSetIgnoreForcedBlank(g_ppu, DcNoBlankEnabled());
        PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL);
        return;
    }

    /* Stage gameplay: BG1/BG2 extend into the margins (true extra level,
     * not side art). BG3 stays clamped so a HUD/overlay cannot wrap. The
     * Demon's Crest status-bar remap is not used: this title's HUD is not
     * that BG3 band. World-keyed history uses each layer's live PPU scroll
     * as its origin (no DC camera RAM mirrors here). */
    PpuSetWidescreenLayerClamp(g_ppu, 0x04);
    PpuSetWidescreenLayerMask(g_ppu, 0);
    PpuSetWidescreenBg3Widen(g_ppu, 0);
    PpuSetWsHudOamShiftRange(g_ppu, 0, 0);
    PpuSetWsHudOamShiftRange2(g_ppu, 0, 0);
    PpuSetWidescreenLayerElasticBand(g_ppu, 2, 0, 0, NULL, 0);
    PpuSetWidescreenLineEnhancer(g_ppu, NULL, NULL);
    PpuSetIgnoreForcedBlank(g_ppu, DcNoBlankEnabled());
    DcAnchorStatusBar(); /* no-op unless SNESRECOMP_WS_HUD=1 */

    if (DcHistoryEnabled()) {
        DcRegisterLayerHistory(0, g_ppu->hScroll[0], g_ppu->vScroll[0]);
        DcRegisterLayerHistory(1, g_ppu->hScroll[1], g_ppu->vScroll[1]);
    }

    static unsigned dbg;
    if (getenv("SNESRECOMP_WS_DEBUG") && (++dbg % 60) == 1) {
        fprintf(stderr,
                "[cd-ws] mode=%d inidisp=%02X en=%02X bgXsc=%02X/%02X/%02X "
                "h=%u/%u v=%u/%u extra=%d stage=1\n",
                g_ppu->bgmode & 7, g_ppu->inidisp, g_ppu->screenEnabled[0],
                g_ppu->bgXsc[0], g_ppu->bgXsc[1], g_ppu->bgXsc[2],
                g_ppu->hScroll[0], g_ppu->hScroll[1],
                g_ppu->vScroll[0], g_ppu->vScroll[1], g_ws_extra);
    }
}

void DcWidescreenAfterRaster(void) {
    if (!g_ppu || !DcDisplay_IsWidescreenEnabled())
        return;
    /* Capture this frame's native viewport into the world-keyed store. The
     * game's own VRAM uploads are captured at write time by the framework,
     * so ahead-of-view staging is already in the history. */
    WsShadowFrame(g_ppu);
    DcMaybePrintMarginStats();
}

/* ─────────────────────── Fase 4: objects and sprites ──────────────────────
 *
 * The authentic object loop (bank $82) culls an object when its camera-
 * relative position leaves a fixed window:
 *
 *     dx = objX - camX;   keep if (dx + 0x60) < 0x1C0      (X)
 *     dy = objY - camY;   keep if (dy + 0x60) < 0x1A0      (Y)
 *
 * i.e. dx in [-0x60, 0x160) = [-96, 352): the 256-px native view plus 96 px
 * of slack on each side (sprite widths, activation). The injected hook
 * (tools/dc_apply_widescreen_hooks.py) replaces the two constants with
 * 0x60 + margin and 0x1C0/0x1A0 + 2*margin, which widens BOTH sides by
 * `margin` without moving the window's base. At margin 0 the expressions
 * evaluate to the authentic constants, so the 4:3 path stays bit-identical.
 *
 * The kept object then computes its screen X/Y ($10/$12), sets its visible
 * bit ($00 bit 4) and writes its OAM entry — which is what puts it in the
 * margins.
 *
 * The sprite emitter (the bank $80/$00 function at $F689, an AOT root) is
 * the other half: its X test at $F755 hides every sprite whose 9-bit OAM X is
 * >= 256 with a low byte < 0xF3. The injected hook keeps the right margin
 * (low byte < margin) and the widened left-edge window (low byte >=
 * 0xF3 - margin) instead, so sprites that the authentic frame clips at the
 * screen edge survive in the margins; everything else keeps the authentic
 * $E0 hide. The framework decodes 256..256+extra as positive (right margin)
 * and >= that as the wrapped left margin (PpuDecodeOamX).
 *
 * Gate: the margins are only widened during stage gameplay with widescreen
 * on — a pillarboxed cutscene must not reveal objects the authentic frame
 * hides. SNESRECOMP_WS_OAM=0 disables both halves for A/B captures.
 */

static int DcOamPolicyEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *env = getenv("SNESRECOMP_WS_OAM");
        enabled = env && env[0] ? (env[0] != '0') : 1;
    }
    return enabled;
}

/* Diagnostic/performance cap: SNESRECOMP_WS_CULL=<px> limits how far beyond
 * the native view objects keep updating and sprites keep emitting. 0 keeps
 * the authentic cull window (no margin objects); unset uses the live margin.
 * Useful to measure the guest-side cost of the widened object set. */
static int DcCullMarginOverride(void) {
    static int value = -2;
    if (value == -2) {
        const char *env = getenv("SNESRECOMP_WS_CULL");
        value = env && env[0] ? atoi(env) : -1;
        if (value < 0)
            value = -1;
    }
    return value;
}

int DcWsLiveMargin(void) {
    if (!DcOamPolicyEnabled())
        return 0;
    if (!DcDisplay_IsWidescreenEnabled() || !DcDisplay_IsWidescreenActive())
        return 0;
    if (!DcStageGameplay())
        return 0;
    if (g_ws_extra <= 0)
        return 0;
    /* The left-edge window is 13 + margin pixels of a 9-bit OAM position;
     * 0xF3 - margin must stay positive (kPpuExtraLeftRight caps at 272). */
    int m = g_ws_extra > 0xF0 ? 0xF0 : g_ws_extra;
    int cap = DcCullMarginOverride();
    return (cap >= 0 && cap < m) ? cap : m;
}

int DcWsObjectCullMargin(void) { return DcWsLiveMargin(); }

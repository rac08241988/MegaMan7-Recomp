/*
 * Mega Man 7 — widescreen presentation geometry.
 *
 * Pure math over the framework's SnesDisplayAspect helpers; nothing here
 * touches guest state. See docs/WIDESCREEN_PLAN.md.
 */

#include "dc_display.h"

#include <stddef.h>

#include "desktop/config.h"
#include "host_main.h"
#include "snes/ppu.h"
#include "widescreen.h"

/* kPpuBufWidth (800 = 256 + 2*272) is the host's native-widescreen surface;
 * wider requests are rejected by PreparePpuFrame and fall back to 256. */
static int ClampEven(int64_t value) {
    value &= ~1;
    if (value < 256)
        value = 256;
    if (value > kPpuBufWidth)
        value = kPpuBufWidth;
    return (int)value;
}

static double DcAspectTarget(DcRenderAspect aspect, int width, int height) {
    switch (aspect) {
    case DC_ASPECT_16_9:
        return 16.0 / 9.0;
    case DC_ASPECT_21_9:
        return 21.0 / 9.0;
    case DC_ASPECT_32_9:
        return 32.0 / 9.0;
    default:
        /* Adaptive: follow the drawable, falling back to 16:9 before a window
         * exists. The helper below still clamps to the 4:3 floor and the
         * kPpuBufWidth surface cap. */
        return width > 0 && height > 0 ? (double)width / (double)height
                                       : 16.0 / 9.0;
    }
}

void DcDisplay_SetWidescreenEnabled(bool enabled) {
    snesrecomp_desktop_set_widescreen(enabled);
}

bool DcDisplay_IsWidescreenEnabled(void) { return g_config.widescreen; }

bool DcDisplay_IsWidescreenActive(void) { return g_ws_active; }

int DcDisplay_ComputeFrameWidth(DcRenderAspect aspect, int drawable_width,
                                int drawable_height,
                                SnesDisplayAspect display_aspect) {
    /* Deterministic margin for probes/CI: SNESRECOMP_WS_EXTRA overrides the
     * window-derived width regardless of aspect. -1 follows the window. */
    int override_extra = PpuWsExtraOverride();
    if (override_extra >= 0)
        return ClampEven(256 + 2 * override_extra);

    SnesDisplayFrame frame = SnesDisplayAspect_ComputeAdaptiveFrame(
        256, DC_FRAME_HEIGHT, kPpuBufWidth,
        DcAspectTarget(aspect, drawable_width, drawable_height),
        display_aspect);
    return ClampEven(frame.width);
}

void DcDisplay_ComputeViewport(int source_width, int source_height,
                               int drawable_width, int drawable_height,
                               SnesDisplayAspect display_aspect,
                               bool ignore_aspect, bool integer_scale,
                               DcDisplayViewport *viewport) {
    SnesDisplayAspect_ComputeViewport(
        source_width, source_height, drawable_width, drawable_height,
        display_aspect, ignore_aspect, integer_scale, viewport);
    /* Widescreen path: never present larger than 1080p (1920x1080). The
     * SNES field itself stays ~16:9 x 224; this only caps the window blit.
     * Used by desktop and by the Switch merge of this same module. */
    if (viewport && DcDisplay_IsWidescreenEnabled()) {
        const int max_w = 1920, max_h = 1080;
        if (viewport->width > max_w || viewport->height > max_h) {
            double sx = (double)max_w / (double)viewport->width;
            double sy = (double)max_h / (double)viewport->height;
            double s = sx < sy ? sx : sy;
            int nw = ((int)(viewport->width * s)) & ~1;
            int nh = ((int)(viewport->height * s)) & ~1;
            if (nw < 2) nw = 2;
            if (nh < 2) nh = 2;
            viewport->x += (viewport->width - nw) / 2;
            viewport->y += (viewport->height - nh) / 2;
            viewport->width = nw;
            viewport->height = nh;
        }
    }
}

int DcDisplay_GetWindowBaseWidth(int frame_width,
                                 SnesDisplayAspect display_aspect) {
    return SnesDisplayAspect_ComputeWindowWidth(
        frame_width, DC_FRAME_HEIGHT, DcDisplay_GetWindowBaseHeight(),
        display_aspect);
}

int DcDisplay_GetWindowBaseHeight(void) { return 240; }

/* ==========================================================================
 * PPU-16 — hal.c: capability autodetection (host default).
 * --------------------------------------------------------------------------
 * The host build reports a conservative loopback target. A port overrides
 * ppu16_hal_detect() (declare its prototype, provide its definition — the
 * weak default below makes that safe) with SoC-specific probing: heap
 * caps, PSRAM size, panel link frequency, PPA presence.
 * ========================================================================== */
#include "ppu16/hal.h"

#if defined(__GNUC__)
__attribute__((weak))
#endif
ppu16_caps_t ppu16_hal_detect(void)
{
    ppu16_caps_t c;
    c.target_name = "host";
    c.cpu_hz      = 0;
    c.sram_bytes  = 0;
    c.psram_bytes = 0;
    c.panel_bps   = 0;
    c.has_ppa     = 0;
    c.has_dma     = 0;
    c.has_fpu     = 1;
    return c;
}

ppu16_mode_t ppu16_caps_recommend_mode(const ppu16_caps_t *c,
                                       unsigned w, unsigned h,
                                       uint32_t *out_fps_ceiling)
{
    /* The panel link is the wall that outranks everything (performance.md */
    /* §10): if the ceiling cannot carry 60 fps of full-frame 16bpp, no     */
    /* render mode fixes it — reduce the active window or the interface.   */
    uint32_t fps = ppu16_caps_panel_fps(c, w, h);
    if (out_fps_ceiling) *out_fps_ceiling = fps;

    if (fps == 0) return PPU16_MODE_TEXT;          /* unknown panel: safe  */
    if (fps >= 60 && c->sram_bytes >= 128u * 1024u) return PPU16_MODE_AFFINE;
    if (fps >= 60)                                  return PPU16_MODE_RICH;
    if (fps >= 30)                                  return PPU16_MODE_RICH;
    return PPU16_MODE_TEXT;
}

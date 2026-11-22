/* ==========================================================================
 * PPU-16 — hal.h: hardware capability autodetection and recommendations.
 * --------------------------------------------------------------------------
 * The core never assumes hardware (AGENTS.md rule 5); this is the only
 * place parameters that matter are described. A port overrides
 * ppu16_hal_detect(); the host build returns a loopback target. From the
 * detected capabilities the library recommends a display mode using the
 * bandwidth model in docs/performance.md — the panel link is the wall
 * that outranks everything, so it is measured first.
 * ========================================================================== */
#ifndef PPU16_HAL_H
#define PPU16_HAL_H

#include "types.h"
#include "ppu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *target_name;
    uint32_t    cpu_hz;
    uint32_t    sram_bytes;
    uint32_t    psram_bytes;
    uint32_t    panel_bps;         /* effective panel link, bits per second */
    uint8_t     has_ppa;           /* 2D-DMA blitter (ESP32-P4 class)       */
    uint8_t     has_dma;
    uint8_t     has_fpu;
} ppu16_caps_t;

/* Detect the running target. Ports override; host returns "host".      */
ppu16_caps_t ppu16_hal_detect(void);

/* Panel-link fps ceiling for a full-frame 16bpp scanout at w x h.      */
static inline uint32_t ppu16_caps_panel_fps(const ppu16_caps_t *c,
                                            unsigned w, unsigned h)
{
    uint32_t bytes = (uint32_t)w * h * 2u;
    if (!bytes || c->panel_bps < 8u) return 0;
    return c->panel_bps / 8u / bytes;
}

/* Recommend a display mode from capabilities. Verdict logic follows    */
/* docs/performance.md §10: the panel interface decides viability first, */
/* chip second.                                                          */
ppu16_mode_t ppu16_caps_recommend_mode(const ppu16_caps_t *c,
                                       unsigned w, unsigned h,
                                       uint32_t *out_fps_ceiling);

#ifdef __cplusplus
}
#endif
#endif /* PPU16_HAL_H */

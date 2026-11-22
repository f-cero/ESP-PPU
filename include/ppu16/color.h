/* ==========================================================================
 * PPU-16 — color.h: RGB565 construction and console-style colour math.
 * --------------------------------------------------------------------------
 * All operations are branchless where practical and exact — the tests pin
 * the arithmetic bit for bit.  The blend ops mirror the SNES/GBA colour
 * math units: additive, subtractive, and half variants, saturating.
 * ========================================================================== */
#ifndef PPU16_COLOR_H
#define PPU16_COLOR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPU16_R565(c)  (uint8_t)(((c) >> 11) & 0x1F)
#define PPU16_G565(c)  (uint8_t)(((c) >> 5)  & 0x3F)
#define PPU16_B565(c)  (uint8_t)((c) & 0x1F)

static inline uint16_t ppu16_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
}

/* Componentwise saturating add (SNES "colour addition").               */
static inline uint16_t ppu16_blend_add(uint16_t c1, uint16_t c2)
{
    unsigned r = PPU16_R565(c1) + PPU16_R565(c2); if (r > 31u) r = 31u;
    unsigned g = PPU16_G565(c1) + PPU16_G565(c2); if (g > 63u) g = 63u;
    unsigned b = PPU16_B565(c1) + PPU16_B565(c2); if (b > 31u) b = 31u;
    return ppu16_rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* Componentwise saturating subtract (SNES "colour subtraction").       */
static inline uint16_t ppu16_blend_sub(uint16_t c1, uint16_t c2)
{
    int r = (int)PPU16_R565(c1) - (int)PPU16_R565(c2); if (r < 0) r = 0;
    int g = (int)PPU16_G565(c1) - (int)PPU16_G565(c2); if (g < 0) g = 0;
    int b = (int)PPU16_B565(c1) - (int)PPU16_B565(c2); if (b < 0) b = 0;
    return ppu16_rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/* Halved variant: (c1 OP c2) / 2 — the console "half" colour math.     */
static inline uint16_t ppu16_blend_add_half(uint16_t c1, uint16_t c2)
{   return (uint16_t)((ppu16_blend_add(c1, c2) & 0xF7DE) >> 1); }  /* per-channel >>1 */

static inline uint16_t ppu16_blend_sub_half(uint16_t c1, uint16_t c2)
{   return (uint16_t)((ppu16_blend_sub(c1, c2) & 0xF7DE) >> 1); }

/* Fade c toward `to` by level/32 (brightness engine). level 0..32.     */
static inline uint16_t ppu16_fade(uint16_t c, uint16_t to, uint8_t level)
{
    unsigned r = PPU16_R565(c)   + ((PPU16_R565(to)   - PPU16_R565(c))   * level) / 32;
    unsigned g = PPU16_G565(c)   + ((PPU16_G565(to)   - PPU16_G565(c))   * level) / 32;
    unsigned b = PPU16_B565(c)   + ((PPU16_B565(to)   - PPU16_B565(c))   * level) / 32;
    return ppu16_rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

#ifdef __cplusplus
}
#endif
#endif /* PPU16_COLOR_H */

/* ==========================================================================
 * PPU-16 — bg.h: tile background layers.
 * --------------------------------------------------------------------------
 * Formats (matching docs/api.md; the asset tool emits exactly these):
 *
 *  Tileset, 8x8 tiles:
 *    2bpp: 16 B/tile, 2 px/byte, low 2 bits = left pixel
 *    4bpp: 32 B/tile, 2 px/byte, low nibble  = left pixel
 *    8bpp: 64 B/tile, 1 B/px
 *
 *  Map entry — 16-bit little-endian, SNES-style packing:
 *    bits  0-9   tile index (into tileset)
 *    bits 10-12  palette slot (4bpp: 16-colour slot; 2bpp: 4-colour slot)
 *    bit  13     priority override (else layer priority)
 *    bit  14     horizontal flip
 *    bit  15     vertical flip
 *
 *  Colour index 0 is transparent for all bpp.
 *  Map dimensions are powers of two in tiles; wrapping uses masks.
 * ========================================================================== */
#ifndef PPU16_BG_H
#define PPU16_BG_H

#include "types.h"
#include "color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Map-entry flag/packing bits. */
#define PPU16_ME_PRIORITY  0x2000u
#define PPU16_ME_HFLIP     0x4000u
#define PPU16_ME_VFLIP     0x8000u
#define PPU16_ME_PALSHIFT  10u
#define PPU16_ME_PALMASK   0x7000u
#define PPU16_ME_TILEMASK  0x03FFu

typedef struct {
    /* Geometry. */
    int16_t      scroll_x, scroll_y;        /* base scroll, pixels          */
    /* HDMA-style per-scanline scroll deltas, height entries each, or NULL. */
    const int16_t *scroll_x_table;
    const int16_t *scroll_y_table;

    /* Memory. */
    const uint8_t  *tiles;                  /* tileset                      */
    const uint16_t *map;                    /* tile map, row major          */
    uint8_t        bpp;                     /* 2, 4 or 8                    */
    uint8_t        map_shift_x;             /* log2(map width  in tiles)    */
    uint8_t        map_shift_y;             /* log2(map height in tiles)    */

    /* State. */
    uint8_t        priority;                /* 0 front … 3 back             */
    uint8_t        pal_offset;              /* added to map palette slot    */
    uint8_t        win_bits;                /* window membership, see ppu.h */
} ppu16_bg_t;

/* Fetch one map entry (wraps with masks; no bounds checks by design).   */
static inline uint16_t ppu16_bg_entry(const ppu16_bg_t *bg, int tx, int ty)
{
    unsigned mw = 1u << bg->map_shift_x;
    unsigned mh = 1u << bg->map_shift_y;
    unsigned x  = (unsigned)tx & (mw - 1u);
    unsigned y  = (unsigned)ty & (mh - 1u);
    return bg->map[y * mw + x];
}

/* Fetch one texel from a tileset (fx/fy already flip-resolved).         */
static inline unsigned ppu16_tile_texel(const uint8_t *tiles, uint8_t bpp,
                                        unsigned tile, unsigned fx, unsigned fy)
{
    const uint8_t *t = tiles + ((size_t)tile << (bpp == 8 ? 6 : bpp == 4 ? 5 : 4))
                     + (size_t)fy * (bpp == 8 ? 8 : 4);
    if (bpp == 8)  return t[fx];
    if (bpp == 4)  return (fx & 1u) ? (t[fx >> 1] >> 4) : (t[fx >> 1] & 0xFu);
    return (fx & 1u) ? ((t[fx >> 1] >> 2) & 0x3u) : (t[fx >> 1] & 0x3u);
}

#ifdef __cplusplus
}
#endif
#endif /* PPU16_BG_H */

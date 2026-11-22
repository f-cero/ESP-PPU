/* ==========================================================================
 * PPU-16 — sprite.h: OAM sprites (objects).
 * --------------------------------------------------------------------------
 * Sprites use the same tile formats as backgrounds (bg.h) from a separate
 * tileset. Sprites up to 64x64, 4 or 8 bpp, H/V flip, per-sprite priority
 * and, 16-bit-class, affine (rotating) sprites: one of PPU16_MAX_MAT
 * 2x2 matrices is referenced by index and applied centred on the sprite,
 * evaluated per scanline and incremented per pixel — again with no
 * per-pixel multiply.
 * ========================================================================== */
#ifndef PPU16_SPRITE_H
#define PPU16_SPRITE_H

#include "types.h"
#include "affine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PPU16_SF_HFLIP   0x01
#define PPU16_SF_VFLIP   0x02

typedef struct {
    int16_t  x, y;         /* top-left on the output surface               */
    uint8_t  w, h;         /* 8..64, multiple of 8                         */
    uint8_t  tile;         /* base tile; tiles are row-major w/8 across    */
    uint8_t  pal;          /* 4bpp: 16-colour slot within the sprite bank  */
    uint8_t  bpp;          /* 4 or 8                                       */
    uint8_t  priority;     /* 0 front … 3 back                             */
    uint8_t  flags;        /* PPU16_SF_*                                   */
    int8_t   affine_id;    /* -1 = none, else matrix index (0..15)         */
    uint8_t  win_bits;     /* window membership, see ppu.h                 */
} ppu16_oam_entry_t;

/* Sprite tile layout: tiles advance left-to-right, then top-to-bottom. */
static inline unsigned ppu16_spr_tile_index(const ppu16_oam_entry_t *s,
                                            unsigned tx, unsigned ty)
{
    return (unsigned)s->tile + ty * (unsigned)(s->w / PPU16_TILE) + tx;
}

#ifdef __cplusplus
}
#endif
#endif /* PPU16_SPRITE_H */

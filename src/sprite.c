/* ==========================================================================
 * PPU-16 — sprite.c: OAM span renderer (plain + affine sprites).
 * --------------------------------------------------------------------------
 * Plain sprites are a direct tile fetch. Affine sprites use a 2x2 matrix
 * centred on the sprite (the GBA model): the sample point is evaluated
 * once per scanline at the sprite's left edge and INCREMENTED per pixel
 * — no per-pixel multiply, same invariant as the affine layers.
 * ========================================================================== */
#include "ppu16/ppu16.h"

/* Fetch a sprite texel from the sprite tileset.                        */
static inline unsigned spr_texel(const ppu16_t *p, const ppu16_oam_entry_t *s,
                                 unsigned tx, unsigned ty, unsigned fx, unsigned fy)
{
    unsigned tile = ppu16_spr_tile_index(s, tx, ty);
    return ppu16_tile_texel(p->spr_tiles, s->bpp, tile, fx, fy);
}

void ppu16_spr_render_span(ppu16_t *p, const ppu16_oam_entry_t *s, int y)
{
    const int w = s->w;
    /* Horizontal clip against the output surface.                      */
    int x0 = s->x, x1 = s->x + w;
    if (x0 < 0) x0 = 0;
    if (x1 > p->cfg.width) x1 = p->cfg.width;
    p->span_x0 = x0; p->span_x1 = x1;
    if (x1 <= x0) return;

    uint16_t *sc = p->span_color;
    uint8_t  *op = p->span_opaque;

    int ly = y - s->y;                          /* 0..h-1, not flipped yet */
    if (s->flags & PPU16_SF_VFLIP) ly = s->h - 1 - ly;

    if (s->affine_id >= 0) {
        /* --- Affine sprite: incremental centred transform. ---------- */
        const ppu16_mat_t *m = &p->spr_mat[s->affine_id & (PPU16_MAX_MAT - 1)];
        const int hw = w / 2, hh = s->h / 2;
        /* Sample point at output x = s->x (left edge):                  */
        /*   t = M * (x - cx, ly - cy) + c                               */
        int32_t u = (int32_t)(((int64_t)m->a * (-hw)) >> 0)
                  + (int32_t)(((int64_t)m->b * (ly - hh)) >> 0) + (hw << 16);
        int32_t v = (int32_t)(((int64_t)m->c * (-hw)) >> 0)
                  + (int32_t)(((int64_t)m->d * (ly - hh)) >> 0) + (hh << 16);
        /* Offsets are plain pixel counts, so M * offset is 16.16 after  */
        /* the multiply; the centre re-add is pre-shifted.               */

        for (int x = x0; x < x1; x++) {
            const int32_t iu = u >> 16, iv = v >> 16;
            if (iu >= 0 && iu < w && iv >= 0 && iv < s->h) {
                unsigned ti = spr_texel(p, s,
                                        (unsigned)iu >> 3, (unsigned)iv >> 3,
                                        (unsigned)iu & 7u, (unsigned)iv & 7u);
                if (ti != 0) {
                    sc[x] = (s->bpp == 8)
                          ? p->pal_spr[ti]
                          : p->pal_spr[(unsigned)s->pal * 16u + ti];
                    op[x] = 1;
                } else op[x] = 0;
            } else op[x] = 0;
            u += m->a;
            v += m->c;
        }
    } else {
        /* --- Plain sprite: direct tile fetch with H flip. ------------ */
        for (int x = x0; x < x1; x++) {
            int fx = x - s->x;
            if (s->flags & PPU16_SF_HFLIP) fx = w - 1 - fx;
            unsigned ti = spr_texel(p, s,
                                    (unsigned)fx >> 3, (unsigned)ly >> 3,
                                    (unsigned)fx & 7u, (unsigned)ly & 7u);
            if (ti != 0) {
                sc[x] = (s->bpp == 8)
                      ? p->pal_spr[ti]
                      : p->pal_spr[(unsigned)s->pal * 16u + ti];
                op[x] = 1;
            } else op[x] = 0;
        }
    }
}

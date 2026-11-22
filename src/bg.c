/* ==========================================================================
 * PPU-16 — bg.c: tile background span renderer.
 * --------------------------------------------------------------------------
 * Renders one scanline of one BG layer into the shared span buffers
 * (span_color / span_opaque). Per-pixel map lookup keeps the code honest
 * and small; the map lives in fast memory and the lookup is two adds,
 * two loads and a mask — measure before "optimising" it (AGENTS.md).
 * ========================================================================== */
#include "ppu16/ppu16.h"

void ppu16_bg_render_span(ppu16_t *p, const ppu16_bg_t *bg, int y)
{
    const int W = p->cfg.width;
    uint16_t *sc = p->span_color;
    uint8_t  *op = p->span_opaque;

    /* HDMA-style per-line scroll: base + table delta.                  */
    int32_t scx = bg->scroll_x + (bg->scroll_x_table ? bg->scroll_x_table[y] : 0);
    int32_t scy = bg->scroll_y + (bg->scroll_y_table ? bg->scroll_y_table[y] : 0);

    const int py = scy + y;
    const int ty = py >> 3;
    const int fy = py & 7;

    for (int x = 0; x < W; x++) {
        const int px = scx + x;
        const int tx = px >> 3;
        const int fx = px & 7;

        const uint16_t e = ppu16_bg_entry(bg, tx, ty);
        unsigned tile = e & PPU16_ME_TILEMASK;
        unsigned fxi  = (e & PPU16_ME_HFLIP) ? (unsigned)(7 - fx) : (unsigned)fx;
        unsigned fyi  = (e & PPU16_ME_VFLIP) ? (unsigned)(7 - fy) : (unsigned)fy;

        unsigned ti = ppu16_tile_texel(bg->tiles, bg->bpp, tile, fxi, fyi);
        if (ti == 0) { op[x] = 0; continue; }        /* colour 0: clear   */

        unsigned base;
        if (bg->bpp == 8)       base = 0;
        else if (bg->bpp == 4)  base = (((e >> PPU16_ME_PALSHIFT) & 7u) + bg->pal_offset) * 16u;
        else                    base = (((e >> PPU16_ME_PALSHIFT) & 7u) + bg->pal_offset) * 4u;

        sc[x] = p->pal_bg[base + ti];
        op[x] = 1;
    }
}

/* ==========================================================================
 * PPU-16 — affine.c: Mode 7-class affine layers and the QFA engine.
 * --------------------------------------------------------------------------
 * Invariants (docs/performance.md; enforce in review):
 *   - the sampler never multiplies: per row it reads a start point and an
 *     x-step from the row-matrix table, then adds per pixel;
 *   - wrap is `& mask`, clamp is branchless, never a modulo;
 *   - the doubling stage (dup_*) performs no arithmetic per output pixel.
 * ========================================================================== */
#include "ppu16/ppu16.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Row-matrix table builders                                            */
/* ------------------------------------------------------------------ */

void ppu16_affine_fold_rowmat(ppu16_mat6_t *out, int rows,
                              const ppu16_mat6_t *m)
{
    /* Fold the per-row terms b*y and d*y into e/f once, at scene setup, */
    /* so the sampler only ever reads e/f and adds a/c per pixel.        */
    for (int y = 0; y < rows; y++) {
        out[y].a = m->a;  out[y].c = m->c;
        out[y].b = m->b;  out[y].d = m->d;
        /* b (16.16) * y (integer rows) stays in 16.16 — no shift, but   */
        /* the multiply goes through 64-bit to stay overflow-safe.       */
        out[y].e = m->e + (int32_t)((int64_t)m->b * y);
        out[y].f = m->f + (int32_t)((int64_t)m->d * y);
    }
}

void ppu16_affine_perspective_fill(ppu16_mat6_t *out, int rows,
                                   int yh, int32_t cam_h, int32_t tex_per_u,
                                   int32_t cos_t, int32_t sin_t)
{
    /* step(y) = cam_h * tex_per_u / (y - yh): cam_h is whole world       */
    /* units, tex_per_u is 16.16 — the product lands directly in 16.16.   */
    /* One divide per scanline at setup, never in the sampler (§7.1).     */
    const int32_t STEP_MAX = 8 * PPU16_FP16_ONE;   /* near-field clamp    */
    for (int y = 0; y < rows; y++) {
        ppu16_mat6_t *m = &out[y];
        int dy = y - yh;
        if (dy <= 0) {                    /* above the horizon: sky      */
            m->a = m->b = m->c = m->d = m->e = m->f = 0;
            continue;
        }
        int32_t step = (int32_t)(((int64_t)cam_h * tex_per_u) / dy);
        if (step > STEP_MAX) step = STEP_MAX;
        m->a = (int32_t)(((int64_t)step * cos_t) >> 16);
        m->c = (int32_t)(((int64_t)step * sin_t) >> 16);
        m->b = m->a;                      /* isotropic plane             */
        m->d = m->c;
        m->e = 0; m->f = 0;               /* camera centre applied by    */
    }                                     /* game code before folding    */
}

/* ------------------------------------------------------------------ */
/* LOD                                                                  */
/* (ppu16_lod_for_step is a static inline in affine.h)                  */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Samplers                                                             */
/* ------------------------------------------------------------------ */

/* Sample one texel with wrap or clamp at mip `lod`.                    */
static inline unsigned sample_map(const ppu16_affine_t *L, uint8_t lod,
                                  int32_t u, int32_t v)
{
    const uint8_t *base = L->mip[lod] ? L->mip[lod] : L->mip[0];
    const unsigned shift = 8u - lod;          /* mip lod is (256>>lod)^2 */
    const unsigned mask  = 0xFFu >> lod;
    int32_t iu = u >> 16, iv = v >> 16;
    if (L->flags & PPU16_AF_WRAP) {
        return base[((unsigned)iv & mask) << shift | ((unsigned)iu & mask)];
    } else {
        const int32_t max = (int32_t)mask;
        if (iu < 0) iu = 0; else if (iu > max) iu = max;
        if (iv < 0) iv = 0; else if (iv > max) iv = max;
        return base[((unsigned)iv << shift) | (unsigned)iu];
    }
}

static inline const uint16_t *palette_for(const ppu16_affine_t *L, int local)
{
    if (L->palette_rows && L->palette_rows_count) {
        if (local < L->palette_rows_count) return L->palette_rows[local];
        return L->palette_rows[L->palette_rows_count - 1];
    }
    return L->palette;
}

/* Generic full-res sampler into an RGB565 buffer (also the QFA engine's */
/* quarter-res sampler — identical math, denser table, see handoff §5.2).*/
void ppu16_affine_sample_buf(uint16_t *dst, int dst_stride, int w, int h,
                             const ppu16_affine_t *L,
                             const ppu16_mat6_t *rowmat)
{
    for (int y = 0; y < h; y++) {
        const ppu16_mat6_t *m = &rowmat[y];
        const uint16_t *pal = palette_for(L, y);
        int biased = (int)ppu16_lod_for_step(m->a, m->c) + (int)L->lod_bias;
        if (biased < 0) biased = 0;
        else if (biased > PPU16_MAX_MIP - 1) biased = PPU16_MAX_MIP - 1;
        const uint8_t lod = (uint8_t)biased;
        int32_t sx = m->e;
        int32_t sy = m->f;
        uint16_t *d = dst + (size_t)y * dst_stride;
        const bool key = (L->flags & PPU16_AF_ALPHAKEY) != 0;

        /* Fast path: constant v across the row (axis-aligned scroll).   */
        if (m->c == 0 && !key) {
            for (int x = 0; x < w; x++) {
                d[x] = pal[sample_map(L, lod, sx, sy)];
                sx += m->a;                      /* v never moves         */
            }
        } else {
            for (int x = 0; x < w; x++) {
                unsigned ti = sample_map(L, lod, sx, sy);
                if (key && ti == 0) { d[x] = 0; }
                else d[x] = pal[ti];
                sx += m->a;
                sy += m->c;
            }
        }
    }
}

/* Span entry: renders the layer's band of one scanline into the shared  */
/* span buffers. The span is written in output coordinates dest_x..      */
void ppu16_affine_render_span(ppu16_t *p, const ppu16_affine_t *L, int y)
{
    const int local = y - L->dest_y;
    int w = L->dest_w;

    /* Clip horizontally against the output surface.                    */
    int x0 = L->dest_x, x1 = L->dest_x + w;
    if (x0 < 0) { w += x0; x0 = 0; }
    if (x1 > p->cfg.width) { w = p->cfg.width - x0; x1 = p->cfg.width; }
    p->span_x0 = x0; p->span_x1 = x1;
    if (w <= 0) { p->span_x1 = p->span_x0; return; }

    const int mi = local < (int)L->rowmat_count ? local : (int)L->rowmat_count - 1;
    const ppu16_mat6_t *m = &L->rowmat[mi];
    const uint16_t *pal = palette_for(L, local);
    const bool key = (L->flags & PPU16_AF_ALPHAKEY) != 0;
    uint16_t *sc = p->span_color;
    uint8_t  *op = p->span_opaque;

    int32_t sx = m->e;
    int32_t sy = m->f;

    if (L->mode == PPU16_AFF_QFA_H) {
        /* Half horizontal density: sample every other output pixel and  */
        /* duplicate with one SWAR-equivalent store pair. No arithmetic  */
        /* per *sampled* pixel beyond the two adds.                      */
        const int half = (w + 1) / 2;
        int32_t ax2 = m->a << 1, cy2 = m->c << 1;
        for (int i = 0; i < half; i++) {
            const int o = x0 + i * 2;
            unsigned ti = sample_map(L, 0, sx, sy);
            uint16_t c = (key && ti == 0) ? (uint16_t)0 : pal[ti];
            op[o] = (key && ti == 0) ? 0 : 1;
            sc[o] = c;
            if (o + 1 < x1) { sc[o + 1] = c; op[o + 1] = op[o]; }
            sx += ax2; sy += cy2;
        }
    } else {
        uint8_t lod = ppu16_lod_for_step(m->a, m->c);
        int biased = (int)lod + (int)L->lod_bias;
        if (biased < 0) biased = 0; else if (biased > PPU16_MAX_MIP - 1) biased = PPU16_MAX_MIP - 1;
        lod = (uint8_t)biased;
        for (int x = 0; x < w; x++) {
            unsigned ti = sample_map(L, lod, sx, sy);
            if (key && ti == 0) { op[x0 + x] = 0; }
            else { sc[x0 + x] = pal[ti]; op[x0 + x] = 1; }
            sx += m->a;
            sy += m->c;
        }
    }
}

/* ------------------------------------------------------------------ */
/* QFA doubling stage — SWAR duplication, zero arithmetic per output px */
/* ------------------------------------------------------------------ */

/* 2x horizontal: two output pixels per 32-bit store.                   */
void ppu16_affine_dup_h(uint16_t *dst, const uint16_t *src, int n)
{
    uint32_t *d = (uint32_t *)dst;             /* dst 4-byte aligned      */
    for (int i = 0; i < n; i++) {
        uint32_t px = src[i];
        px |= px << 16;                        /* [px][px] in one word    */
        d[i] = px;
    }
}

/* 2x horizontal + 2x vertical: row copy for the vertical, never a      */
/* re-render (handoff §6.1).                                            */
void ppu16_affine_dup_hv(uint16_t *dst, int dst_stride,
                         const uint16_t *src, int src_stride, int w, int h)
{
    for (int y = 0; y < h; y++) {
        uint16_t *row_a = dst + (size_t)(y * 2) * dst_stride;
        uint16_t *row_b = row_a + dst_stride;
        ppu16_affine_dup_h(row_a, src + (size_t)y * src_stride, w);
        memcpy(row_b, row_a, (size_t)w * 2 * sizeof(uint16_t));
    }
}

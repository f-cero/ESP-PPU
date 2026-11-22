/* ==========================================================================
 * PPU-16 — affine.h: Mode 7-class affine layers and the QFA engine.
 * --------------------------------------------------------------------------
 * The transform is the SNES Mode 7 matrix plus translation:
 *
 *      u = a*x + b*y + e        v = c*x + d*y + f        (16.16)
 *
 * evaluated ONCE PER SCANLINE from a row-matrix table (the SNES HDMA model)
 * and INCREMENTED per pixel (two adds).  There is no per-pixel multiply
 * anywhere in this file — that invariant is the whole design (see
 * docs/performance.md and AGENTS.md).
 *
 * QFA ("quarter-fill affine") renders the layer at half resolution in each
 * axis and doubles it on scanout with an arithmetic-free SWAR pass —
 * 4x fewer sampled pixels, period-correct chunky look, trivial cost.
 * ========================================================================== */
#ifndef PPU16_AFFINE_H
#define PPU16_AFFINE_H

#include "types.h"
#include "color.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 6-term 16.16 affine matrix (layer model). */
typedef struct { int32_t a, b, c, d, e, f; } ppu16_mat6_t;

/* 2x2 16.16 matrix (affine sprites, centred transforms).               */
typedef struct { int32_t a, b, c, d; } ppu16_mat_t;

/* Layer render modes. */
typedef enum {
    PPU16_AFF_FULL  = 0,   /* sample every destination pixel              */
    PPU16_AFF_QFA_H = 1,   /* half density horizontally, SWAR expand     */
    PPU16_AFF_QFA   = 2    /* full QFA: used via the frame fast path      */
} ppu16_aff_mode_t;

#define PPU16_AF_WRAP     0x01   /* wrap (mask). 0 = clamp (slower)        */
#define PPU16_AF_ALPHAKEY 0x02   /* palette index 0 is transparent         */

typedef struct {
    /* Memory: mip chain base pointers, LOD0 = 256x256 down to 32x32.      */
    const uint8_t *mip[PPU16_MAX_MIP];
    const uint16_t *palette;                        /* 256-entry RGB565    */
    const uint16_t *const *palette_rows;            /* per-line palettes   */
    uint16_t        palette_rows_count;             /* entries in that tbl */

    /* Row-matrix table: one matrix per output line (HDMA model).  Game    */
    /* code fills it with ppu16_affine_fold_rowmat / _perspective_fill.    */
    const ppu16_mat6_t *rowmat;
    uint16_t            rowmat_count;

    /* Placement on the output surface. */
    int16_t  dest_x, dest_y, dest_w, dest_h;

    uint8_t  mode;        /* ppu16_aff_mode_t                              */
    uint8_t  flags;       /* PPU16_AF_*                                    */
    int8_t   lod_bias;    /* -1 crisper, 0 exact, +1 blurrier              */
    uint8_t  priority;    /* 0 front … 3 back                              */
    uint8_t  win_bits;    /* window membership, see ppu.h                  */
} ppu16_affine_t;

/* ------------------------------------------------------------------ */
/* Row-matrix table builders — run once per frame at scene setup,      */
/* before rendering. The sampler never computes these.                 */
/* ------------------------------------------------------------------ */

/* Fold a single base matrix into `rows` per-line matrices:
 * out[y].e = b*y + e, out[y].f = d*y + f; a,c copied unchanged.
 * After folding, the sampler needs only e/f as its start point.        */
void ppu16_affine_fold_rowmat(ppu16_mat6_t *out, int rows,
                              const ppu16_mat6_t *m);

/* Perspective floor (per-scanline Mode 7, the SNES HDMA trick):
 * rows above the horizon get a zero matrix; step is clamped to 8.0
 * so near-field rows cannot walk off the mip chain.                    */
void ppu16_affine_perspective_fill(ppu16_mat6_t *out, int rows,
                                   int yh, int32_t cam_h, int32_t tex_per_u,
                                   int32_t cos_t, int32_t sin_t);
/* cam_h: camera height above the plane in WHOLE world units (plain int);
 * tex_per_u: texels per world unit, 16.16. Keeping cam_h integral makes
 * the setup divide exact: step(y) = cam_h * tex_per_u / (y - yh).       */

/* LOD selection — L1 approximation of the step magnitude, monotonically
 * related to the true L2 norm, which is all mip selection needs.       */
static inline uint8_t ppu16_lod_for_step(int32_t a, int32_t c)
{
    /* Max of the component magnitudes: exact on axis-aligned steps,      */
    /* and within a half-level of the true L2 norm on diagonals — which   */
    /* is all monotonically-ordered mip selection needs.                  */
    uint32_t ax = (uint32_t)(a < 0 ? -a : a);
    uint32_t cy = (uint32_t)(c < 0 ? -c : c);
    uint32_t mag = ax > cy ? ax : cy;
    if (mag < PPU16_FP16_ONE) return 0;
    uint8_t l = (uint8_t)(31 - __builtin_clz(mag));
    l = (uint8_t)(l - 16);
    return l > (PPU16_MAX_MIP - 1) ? (PPU16_MAX_MIP - 1) : l;
}

/* ------------------------------------------------------------------ */
/* QFA doubling stage — pure data duplication, NO arithmetic per pixel */
/* (rule 2 in AGENTS.md). V3 on PPA-capable targets replaces these     */
/* with a single PPA descriptor; see docs/porting.md.                  */
/* ------------------------------------------------------------------ */
/* Generic full-res sampler into an RGB565 buffer. Also the QFA engine's
 * quarter-res sampler: identical math, denser step, quarter pixel count. */
void ppu16_affine_sample_buf(uint16_t *dst, int dst_stride, int w, int h,
                             const ppu16_affine_t *L,
                             const ppu16_mat6_t *rowmat);

void ppu16_affine_dup_h(uint16_t *dst, const uint16_t *src, int n);
void ppu16_affine_dup_hv(uint16_t *dst, int dst_stride,
                         const uint16_t *src, int src_stride, int w, int h);

#ifdef __cplusplus
}
#endif
#endif /* PPU16_AFFINE_H */

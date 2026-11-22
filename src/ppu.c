/* ==========================================================================
 * PPU-16 — ppu.c: lifecycle, register-file API and the scanline compositor.
 * --------------------------------------------------------------------------
 * The compositor is the heart of the console model. Per scanline it:
 *
 *   1. resolves per-line state (HDMA scroll tables, row matrices, windows),
 *   2. collects the drawables active on that line (BGs, affine layers in
 *      their destination band, sprites whose Y range covers the line),
 *   3. sorts them back-to-front by priority (higher number = further back),
 *   4. for each drawable renders a span (colour + opacity) and merges it
 *      over what is already on the line, honouring windows, colour math
 *      and brightness.
 *
 * Sprites are drawn individually so each carries its own priority — the
 * 16-bit-console behaviour. There is no per-pixel multiply in any span
 * renderer; see AGENTS.md for the invariants a review must enforce.
 * ========================================================================== */
#include "ppu16/ppu16.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

const char *ppu16_err_str(ppu16_err_t e)
{
    switch (e) {
    case PPU16_OK:      return "ok";
    case PPU16_E_PARAM: return "bad argument";
    case PPU16_E_RANGE: return "value out of range";
    case PPU16_E_STATE: return "invalid state";
    case PPU16_E_NOMEM: return "out of memory";
    case PPU16_E_FMT:   return "malformed data";
    }
    return "unknown";
}

static uint8_t mode_n_bg(uint8_t mode)
{
    switch (mode) {
    case PPU16_MODE_TEXT:    return 2;
    case PPU16_MODE_RICH:    return 2;
    case PPU16_MODE_AFFINE:  return 1;
    case PPU16_MODE_AFFINE2: return 0;
    default:                 return 0;
    }
}

static uint8_t mode_n_affine(uint8_t mode)
{
    switch (mode) {
    case PPU16_MODE_AFFINE:  return 1;
    case PPU16_MODE_AFFINE2: return 2;
    default:                 return 0;
    }
}

size_t ppu16_mem_required(const ppu16_config_t *cfg)
{
    if (!cfg || !(cfg->flags & PPU16_CF_QFA_SCRATCH)) return 0;
    /* Half-resolution RGB565 scratch for the full-QFA fast path.        */
    return ((size_t)(cfg->width  + 1) / 2) * ((size_t)(cfg->height + 1) / 2) * 2;
}

ppu16_err_t ppu16_init(ppu16_t *p, const ppu16_config_t *cfg,
                       void *arena, size_t arena_bytes)
{
    if (!p || !cfg)                       return PPU16_E_PARAM;
    if (cfg->width == 0 || cfg->height == 0)          return PPU16_E_RANGE;
    if (cfg->width > PPU16_MAX_W || cfg->height > PPU16_MAX_H) return PPU16_E_RANGE;
    if (cfg->mode > PPU16_MODE_AFFINE2)               return PPU16_E_RANGE;

    memset(p, 0, sizeof *p);
    p->cfg = *cfg;
    p->n_bg    = mode_n_bg(cfg->mode);
    p->n_affine = mode_n_affine(cfg->mode);

    p->fx.backdrop   = 0;
    p->fx.brightness = 0;

    p->arena      = (uint8_t *)arena;
    p->arena_size = arena_bytes;

    if (ppu16_mem_required(cfg) > 0) {
        if (!arena) return PPU16_E_NOMEM;
        if (arena_bytes < ppu16_mem_required(cfg)) return PPU16_E_NOMEM;
        /* Align the scratch to 4 bytes for the SWAR doubling pass.      */
        uintptr_t a = ((uintptr_t)arena + 3u) & ~(uintptr_t)3u;
        p->qfa_buf = (uint16_t *)a;
    }
    return PPU16_OK;
}

ppu16_err_t ppu16_bg_claim(ppu16_t *p, unsigned slot, const ppu16_bg_t *bg)
{
    if (!p || !bg)                 return PPU16_E_PARAM;
    if (slot >= p->n_bg)           return PPU16_E_RANGE;
    if (!bg->map || !bg->tiles)    return PPU16_E_PARAM;
    p->bg[slot] = *bg;
    return PPU16_OK;
}

ppu16_err_t ppu16_affine_claim(ppu16_t *p, unsigned slot,
                               const ppu16_affine_t *aff)
{
    if (!p || !aff)                       return PPU16_E_PARAM;
    if (slot >= p->n_affine)              return PPU16_E_RANGE;
    if (!aff->mip[0] || !aff->palette)    return PPU16_E_PARAM;
    if (!aff->rowmat || aff->rowmat_count == 0) return PPU16_E_PARAM;
    p->affine[slot] = *aff;
    return PPU16_OK;
}

/* ------------------------------------------------------------------ */
/* Windows                                                              */
/* ------------------------------------------------------------------ */

/* Per-line, per-pixel window membership: bit0 = inside win0, bit1 =     */
/* inside win1 (with each window's invert applied).                     */
static void build_win_masks(ppu16_t *p, int y)
{
    const int W = p->cfg.width;
    /* Full per-pixel pass — simple and correct; windows are cheap.     */
    for (int x = 0; x < W; x++) {
        uint8_t mask = 0;
        for (int wi = 0; wi < 2; wi++) {
            const ppu16_window_t *w = &p->win[wi];
            if (!w->enable) continue;
            int inside = (x >= (int)w->x0 && x < (int)w->x1 &&
                          y >= (int)w->y0 && y < (int)w->y1);
            if (w->invert) inside = !inside;
            if (inside) mask |= (uint8_t)(1u << wi);
        }
        p->win_mask[x] = mask;
    }
}

/* Layer visibility through the window system.                          */
static bool win_visible(const ppu16_t *p, uint8_t win_bits, int x)
{
    /* A layer selects windows via win_bits; a single selected window is  */
    /* its own verdict, two selected windows combine via win_logic.       */
    if (!win_bits) return true;
    const bool p0 = (p->win_mask[x] & 1u) != 0;
    const bool p1 = (p->win_mask[x] & 2u) != 0;
    if (win_bits == 1u) return p0;
    if (win_bits == 2u) return p1;
    switch (p->fx.win_logic) {
    case PPU16_WL_AND: return p0 && p1;
    case PPU16_WL_XOR: return p0 != p1;
    case PPU16_WL_NOR: return !p0 && !p1;
    default:           return p0 || p1;
    }
}

/* ------------------------------------------------------------------ */
/* Colour math / fade                                                   */
/* ------------------------------------------------------------------ */

static bool cmath_allows(const ppu16_t *p, uint8_t kind, uint8_t idx)
{
    uint8_t t = p->fx.cmath_targets;
    switch (kind) {
    case 0: return (t & PPU16_CT_BG(idx)) != 0;
    case 1: return (t & PPU16_CT_AFFINE(idx)) != 0;
    default: return (t & PPU16_CT_SPRITES) != 0;
    }
}

static inline uint16_t apply_cmath(const ppu16_t *p, uint16_t c, uint16_t below)
{
    switch (p->fx.cmath_mode) {
    case PPU16_CM_ADD:      return ppu16_blend_add(c, below);
    case PPU16_CM_SUB:      return ppu16_blend_sub(c, below);
    case PPU16_CM_ADD_HALF: return ppu16_blend_add_half(c, below);
    case PPU16_CM_SUB_HALF: return ppu16_blend_sub_half(c, below);
    default:                return c;
    }
}

/* ------------------------------------------------------------------ */
/* Drawables                                                            */
/* ------------------------------------------------------------------ */
#define KIND_BG   0u
#define KIND_AFF  1u
#define KIND_SPR  2u

typedef struct {
    uint8_t kind, idx, priority;
} drawable_t;

/* ------------------------------------------------------------------ */
/* Compositor                                                           */
/* ------------------------------------------------------------------ */

void ppu16_render_line(ppu16_t *p, int y, uint16_t *dst)
{
    const int W = p->cfg.width;
    drawable_t draws[PPU16_MAX_BG + PPU16_MAX_AFFINE + PPU16_MAX_SPRITES];
    int n_draws = 0;
    int i;

    build_win_masks(p, y);

    /* Backdrop.                                                        */
    {
        uint16_t c = p->fx.backdrop;
        if (p->fx.brightness) c = ppu16_fade(c, p->fx.fade_color, p->fx.brightness);
        for (i = 0; i < W; i++) dst[i] = c;
    }

    /* Collect drawables.                                               */
    for (i = 0; i < p->n_bg; i++) {
        if (!p->bg[i].map) continue;          /* unclaimed slot           */
        draws[n_draws].kind = KIND_BG; draws[n_draws].idx = (uint8_t)i;
        draws[n_draws].priority = p->bg[i].priority; n_draws++;
    }
    for (i = 0; i < p->n_affine; i++) {
        const ppu16_affine_t *L = &p->affine[i];
        if (!L->rowmat) continue;
        if (y < L->dest_y || y >= L->dest_y + L->dest_h) continue;
        draws[n_draws].kind = KIND_AFF; draws[n_draws].idx = (uint8_t)i;
        draws[n_draws].priority = L->priority; n_draws++;
    }
    for (i = 0; i < (int)p->oam_count; i++) {
        const ppu16_oam_entry_t *s = &p->oam[i];
        if (y < s->y || y >= s->y + s->h) continue;
        draws[n_draws].kind = KIND_SPR; draws[n_draws].idx = (uint8_t)i;
        draws[n_draws].priority = s->priority; n_draws++;
    }

    /* Stable sort, back (high priority number) first: insertion sort on */
    /* a tiny array — predictable, no library call, stable.              */
    for (i = 1; i < n_draws; i++) {
        drawable_t d = draws[i];
        int j = i - 1;
        while (j >= 0 && draws[j].priority < d.priority) {
            draws[j + 1] = draws[j]; j--;
        }
        draws[j + 1] = d;
    }

    /* Draw back to front.                                              */
    for (i = 0; i < n_draws; i++) {
        const drawable_t *d = &draws[i];
        int x0, x1;

        if (d->kind == KIND_BG) {
            ppu16_bg_render_span(p, &p->bg[d->idx], y);
            x0 = 0; x1 = W;
        } else if (d->kind == KIND_AFF) {
            ppu16_affine_render_span(p, &p->affine[d->idx], y);
            x0 = p->span_x0; x1 = p->span_x1;
        } else {
            ppu16_spr_render_span(p, &p->oam[d->idx], y);
            x0 = p->span_x0; x1 = p->span_x1;
        }
        if (x1 <= x0) continue;

        /* Mosaic: block replicate the span horizontally (per-line the   */
        /* vertical axis is handled by reusing block size on y too).     */
        if (p->fx.mosaic_w > 1 && d->kind != KIND_SPR) {
            int mw = p->fx.mosaic_w;
            for (int x = x0; x < x1; x++) {
                int bx = x0 + ((x - x0) / mw) * mw;
                p->span_color[x]  = p->span_color[bx];
                p->span_opaque[x] = p->span_opaque[bx];
            }
        }

        const uint8_t kind = d->kind, idx = d->idx;
        uint8_t win_bits = 0;
        if (kind == KIND_BG)        win_bits = p->bg[idx].win_bits;
        else if (kind == KIND_AFF)  win_bits = p->affine[idx].win_bits;
        else                        win_bits = p->oam[idx].win_bits;
        const bool blend = (p->fx.cmath_mode != PPU16_CM_OFF) &&
                           cmath_allows(p, kind, idx);

        for (int x = x0; x < x1; x++) {
            if (!p->span_opaque[x]) continue;
            if (!win_visible(p, win_bits, x)) continue;
            uint16_t c = p->span_color[x];
            if (blend) c = apply_cmath(p, c, dst[x]);
            if (p->fx.brightness) c = ppu16_fade(c, p->fx.fade_color, p->fx.brightness);
            dst[x] = c;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Full-QFA fast path: one opaque affine layer covering the whole frame, */
/* no sprites/BGs/windows/colour-math/mosaic. Sample quarter-res into   */
/* the arena scratch, then an arithmetic-free doubling pass. This is    */
/* the V1 pipeline from docs/performance.md — 4x fewer sampled pixels.  */
/* ------------------------------------------------------------------ */
static int qfa_fast_ok(const ppu16_t *p)
{
    if (!(p->cfg.flags & PPU16_CF_QFA_SCRATCH) || !p->qfa_buf) return 0;
    if (p->n_bg != 0 || p->oam_count != 0) return 0;
    if (p->n_affine != 1) return 0;
    const ppu16_affine_t *L = &p->affine[0];
    if (!L->rowmat) return 0;
    if (L->mode != PPU16_AFF_QFA) return 0;
    if (L->dest_x != 0 || L->dest_y != 0 ||
        L->dest_w != p->cfg.width || L->dest_h != p->cfg.height) return 0;
    if (L->flags & PPU16_AF_ALPHAKEY) return 0;
    if (L->win_bits != 0 || p->win[0].enable || p->win[1].enable) return 0;
    if (p->fx.cmath_mode != PPU16_CM_OFF) return 0;
    if (p->fx.mosaic_w > 1 || p->fx.mosaic_h > 1) return 0;
    return 1;
}

void ppu16_render_frame(ppu16_t *p, uint16_t *fb)
{
    if (qfa_fast_ok(p)) {
        const ppu16_affine_t *L = &p->affine[0];
        const int qw = p->cfg.width / 2, qh = p->cfg.height / 2;
        ppu16_mat6_t qmat[PPU16_MAX_H / 2 + 1];
        int rows = qh < (int)L->rowmat_count ? qh : (int)L->rowmat_count;
        for (int i = 0; i < rows; i++) {
            /* Take the full-res row 2i and double the x-step: the qfa   */
            /* pixel covers a 2x2 destination block (see handoff §5.2).  */
            ppu16_mat6_t m = L->rowmat[i * 2];
            m.a <<= 1; m.c <<= 1;
            qmat[i] = m;
        }
        ppu16_affine_sample_buf(p->qfa_buf, qw, qw, rows,
                                L, qmat);
        /* Fill any rows beyond the (clamped) table with the last one.   */
        for (int i = rows; i < qh; i++) qmat[i] = qmat[rows - 1];
        if (rows < qh)
            ppu16_affine_sample_buf(p->qfa_buf + (size_t)rows * qw, qw,
                                    qw, qh - rows, L, qmat + rows);
        ppu16_affine_dup_hv(fb, p->cfg.width, p->qfa_buf, qw, qw, qh);
        if (p->fx.brightness) {
            const size_t n = (size_t)p->cfg.width * p->cfg.height;
            for (size_t k = 0; k < n; k++)
                fb[k] = ppu16_fade(fb[k], p->fx.fade_color, p->fx.brightness);
        }
        p->frame_count++;
        return;
    }

    for (int y = 0; y < p->cfg.height; y++)
        ppu16_render_line(p, y, fb + (size_t)y * p->cfg.width);
    p->frame_count++;
}

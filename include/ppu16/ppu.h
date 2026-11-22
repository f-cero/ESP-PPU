/* ==========================================================================
 * PPU-16 — ppu.h: the PPU itself — state, register-file API, compositor.
 * --------------------------------------------------------------------------
 * This is the header game engines include. The model is a console PPU:
 * you configure layers, palettes, OAM and per-scanline tables (registers),
 * then ask for scanlines or frames of RGB565. There is no general
 * framebuffer API on purpose — see AGENTS.md rule 7.
 *
 * The whole state is one struct (`ppu16_t`, a few KB). Declare it static;
 * the core never allocates. Rendering is deterministic and single-threaded;
 * drive it from your render core/task.
 * ========================================================================== */
#ifndef PPU16_PPU_H
#define PPU16_PPU_H

#include "types.h"
#include "color.h"
#include "bg.h"
#include "affine.h"
#include "sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Colour math / effects registers                                     */
/* ------------------------------------------------------------------ */
typedef enum {
    PPU16_CM_OFF = 0,
    PPU16_CM_ADD,
    PPU16_CM_SUB,
    PPU16_CM_ADD_HALF,
    PPU16_CM_SUB_HALF
} ppu16_cmath_mode_t;

/* cmath_targets bits: which layers are allowed to blend.               */
#define PPU16_CT_BG(i)      (1u << (i))
#define PPU16_CT_AFFINE(i)  (1u << (2 + (i)))
#define PPU16_CT_SPRITES    (1u << 4)

/* Window membership bits (per layer, `win_bits`):
 *   bit0 = layer constrained by window 1
 *   bit1 = layer constrained by window 2
 * Combination logic per `win_logic`; unconstrained bits show everywhere. */
typedef enum {
    PPU16_WL_OR = 0, PPU16_WL_AND, PPU16_WL_XOR, PPU16_WL_NOR
} ppu16_win_logic_t;

typedef struct {
    uint8_t  enable;
    uint8_t  invert;               /* show OUTSIDE the rect               */
    uint16_t x0, y0, x1, y1;       /* inclusive-exclusive rect            */
} ppu16_window_t;

typedef struct {
    uint8_t  cmath_mode;           /* ppu16_cmath_mode_t                  */
    uint8_t  cmath_targets;        /* PPU16_CT_* bitfield                 */
    uint16_t backdrop;             /* colour of empty screen              */
    uint8_t  brightness;           /* 0..32 fade level toward fade_color  */
    uint16_t fade_color;
    uint8_t  mosaic_w, mosaic_h;   /* 0/1 = off, 2..16 = block size       */
    uint8_t  win_logic;            /* ppu16_win_logic_t                   */
} ppu16_fx_t;

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t width, height;        /* output size, <= PPU16_MAX_W/H       */
    uint8_t  mode;                 /* ppu16_mode_t                        */
    uint8_t  flags;
} ppu16_config_t;

/* Ask for the QFA scratch from the arena; enables the full-QFA fast    */
/* path in ppu16_render_frame (4x fewer affine samples).                */
#define PPU16_CF_QFA_SCRATCH 0x01

/* ------------------------------------------------------------------ */
/* The PPU. Public members are the "register file" — set them directly  */
/* (functional style: no accessors for plain data), then render.        */
/* ------------------------------------------------------------------ */
typedef struct ppu16_s {
    ppu16_config_t cfg;

    /* Tile memory — point these at your asset blobs.                    */
    const uint8_t *bg_tiles;
    const uint8_t *spr_tiles;

    /* Layers. Only the first n_bg / n_affine (from the mode) are active. */
    ppu16_bg_t        bg[PPU16_MAX_BG];
    ppu16_affine_t    affine[PPU16_MAX_AFFINE];
    ppu16_oam_entry_t oam[PPU16_MAX_SPRITES];
    uint16_t          oam_count;
    ppu16_mat_t       spr_mat[PPU16_MAX_MAT];   /* affine sprite matrices */

    /* Palette banks.                                                    */
    uint16_t pal_bg[PPU16_MAX_PAL];
    uint16_t pal_spr[PPU16_MAX_PAL];

    ppu16_fx_t fx;
    ppu16_window_t win[2];

    uint8_t  n_bg, n_affine;       /* derived from mode at init           */
    uint32_t frame_count;

    /* --- Private scratch below; not part of the game-facing API.      */
    uint8_t  *arena; size_t arena_size;
    uint16_t *qfa_buf;                         /* optional, from arena     */
    uint16_t  span_color[PPU16_MAX_W];         /* current layer span       */
    uint8_t   span_opaque[PPU16_MAX_W];
    uint8_t   win_mask[PPU16_MAX_W];           /* per-line window masks    */
    int16_t   span_x0, span_x1;                /* active span extent       */
} ppu16_t;

/* Span renderers — shared scratch buffers, called by the compositor.    */
void ppu16_bg_render_span(ppu16_t *p, const ppu16_bg_t *bg, int y);
void ppu16_affine_render_span(ppu16_t *p, const ppu16_affine_t *L, int y);
void ppu16_spr_render_span(ppu16_t *p, const ppu16_oam_entry_t *s, int y);

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/* Bytes of arena needed for this config (0 unless QFA scratch wanted). */
size_t ppu16_mem_required(const ppu16_config_t *cfg);

/* Initialise. Zeroes the state and validates the config. `arena` may be
 * NULL only when ppu16_mem_required(cfg) == 0.                         */
ppu16_err_t ppu16_init(ppu16_t *p, const ppu16_config_t *cfg,
                       void *arena, size_t arena_bytes);

/* Claim a slot for a caller-owned layer description (the library keeps  */
/* the pointer; you keep the storage alive). Slot must be < capacity for */
/* the configured mode.                                                  */
ppu16_err_t ppu16_bg_claim(ppu16_t *p, unsigned slot, const ppu16_bg_t *bg);
ppu16_err_t ppu16_affine_claim(ppu16_t *p, unsigned slot,
                               const ppu16_affine_t *aff);

/* ------------------------------------------------------------------ */
/* Rendering                                                            */
/* ------------------------------------------------------------------ */

/* Render scanline `y` of the current register state into dst (width    */
/* pixels RGB565). This is the primitive scanout callbacks should call. */
void ppu16_render_line(ppu16_t *p, int y, uint16_t *dst);

/* Render a whole frame into fb (width*height RGB565). Uses the full-QFA */
/* fast path when the configuration allows (see docs/architecture.md).   */
void ppu16_render_frame(ppu16_t *p, uint16_t *fb);

#ifdef __cplusplus
}
#endif
#endif /* PPU16_PPU_H */

/* ==========================================================================
 * PPU-16 — types.h: fixed point, limits, error codes, layer modes.
 * --------------------------------------------------------------------------
 * Conventions used across the whole library:
 *
 *  - All geometry is signed 16.16 fixed point: int32_t where 65536 == 1.0.
 *    Products of two 16.16 values need an int64_t intermediate.
 *  - Colours are RGB565 (uint16_t), the native format of the target panels.
 *  - Priorities: 0 = frontmost, 3 = rearmost (SNES-style). Layers with a
 *    numerically larger priority are drawn first and end up behind.
 *  - The core is hardware-agnostic: it never touches a register, never
 *    allocates, and never assumes an endianness except where a wire format
 *    is explicitly defined (see link.h).
 * ========================================================================== */
#ifndef PPU16_TYPES_H
#define PPU16_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Version                                                             */
/* ------------------------------------------------------------------ */
#define PPU16_VERSION_MAJOR 0
#define PPU16_VERSION_MINOR 1
#define PPU16_VERSION_PATCH 0

/* ------------------------------------------------------------------ */
/* Fixed point (16.16)                                                 */
/* ------------------------------------------------------------------ */
#define PPU16_FP16_ONE   0x10000         /* 1.0 in 16.16              */
#define PPU16_FP16_HALF  0x08000         /* 0.5                       */

static inline int32_t ppu16_fp16(int32_t whole, int32_t num, int32_t den)
{
    /* whole + num/den as 16.16, avoiding float entirely. */
    return (whole << 16) + (int32_t)(((int64_t)num << 16) / den);
}

/* ------------------------------------------------------------------ */
/* Compile-time capacity limits.  A ppu16_t is a few KB of state       */
/* (span buffers + palettes + OAM), safe to declare static.            */
/* ------------------------------------------------------------------ */
#define PPU16_MAX_W        1024          /* output width cap          */
#define PPU16_MAX_H        640           /* output height cap         */
#define PPU16_MAX_BG       2             /* tile background layers    */
#define PPU16_MAX_AFFINE   2             /* affine (Mode 7) layers    */
#define PPU16_MAX_SPRITES  128           /* OAM entries               */
#define PPU16_MAX_PAL      256           /* palette entries per bank  */
#define PPU16_MAX_MIP      4             /* affine map mip levels     */
#define PPU16_MAX_MAT      16            /* affine sprite matrices    */
#define PPU16_TILE         8             /* tile edge in pixels       */

/* Upper bound for the init-time arena (QFA scratch at max useful res).  */
#define PPU16_ARENA_MAX    ((PPU16_MAX_W / 2) * (PPU16_MAX_H / 2) * 2)

/* ------------------------------------------------------------------ */
/* Error codes — every fallible entry point returns ppu16_err_t.       */
/* ------------------------------------------------------------------ */
typedef enum {
    PPU16_OK        = 0,
    PPU16_E_PARAM   = -1,   /* NULL pointer / bad argument                  */
    PPU16_E_RANGE   = -2,   /* value outside configured capacity            */
    PPU16_E_STATE   = -3,   /* operation invalid for current state          */
    PPU16_E_NOMEM   = -4,   /* arena too small                              */
    PPU16_E_FMT     = -5    /* malformed wire data (link protocol)          */
} ppu16_err_t;

const char *ppu16_err_str(ppu16_err_t e);

/* ------------------------------------------------------------------ */
/* Console-style display modes.  A mode fixes the set of layers the    */
/* hardware would drive; claiming layers beyond the mode's capacity    */
/* returns PPU16_E_STATE.                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    PPU16_MODE_TEXT    = 0,  /* 2 tile BGs (4bpp) + sprites              */
    PPU16_MODE_RICH    = 1,  /* 1 tile BG 8bpp + 1 tile BG 4bpp + sprites */
    PPU16_MODE_AFFINE  = 2,  /* 1 affine layer + 1 tile BG + sprites     */
    PPU16_MODE_AFFINE2 = 3   /* 2 affine layers + sprites                */
} ppu16_mode_t;

#ifdef __cplusplus
}
#endif
#endif /* PPU16_TYPES_H */

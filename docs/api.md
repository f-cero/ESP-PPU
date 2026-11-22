# API guide

Game code includes exactly one header:

```c
#include "ppu16/ppu16.h"
```

## Lifecycle

```c
ppu16_config_t cfg = { .width = 320, .height = 240,
                       .mode = PPU16_MODE_AFFINE,
                       .flags = PPU16_CF_QFA_SCRATCH };
size_t need = ppu16_mem_required(&cfg);          /* arena for QFA scratch */
static ppu16_t ppu;                              /* ~4 KB of state        */
ppu16_init(&ppu, &cfg, arena, arena_size);
```

Modes fix the layer mix: `PPU16_MODE_TEXT` (2 tile BGs), `PPU16_MODE_RICH`
(8bpp + 4bpp tile BGs), `PPU16_MODE_AFFINE` (1 affine + 1 tile BG),
`PPU16_MODE_AFFINE2` (2 affine layers). Claiming beyond a mode's capacity
returns `PPU16_E_RANGE`.

## Register file

Public struct members *are* the registers — no accessors for plain data:

| Register | Field | Notes |
|---|---|---|
| Layer scroll | `ppu.bg[i].scroll_x/y` | plus per-scanline `scroll_*_table` (HDMA) |
| Palette banks | `ppu.pal_bg`, `ppu.pal_spr` | 256-entry RGB565 each |
| OAM | `ppu.oam[]`, `ppu.oam_count` | up to 128 sprites |
| Sprite matrices | `ppu.spr_mat[16]` | affine sprites |
| Colour math | `ppu.fx.cmath_mode`, `.cmath_targets` | ADD / SUB / half, per-layer enable |
| Backdrop / fade | `ppu.fx.backdrop`, `.brightness`, `.fade_color` | brightness 0..32 |
| Mosaic | `ppu.fx.mosaic_w/h` | block size 2..16 |
| Windows | `ppu.win[2]`, layer `win_bits`, `ppu.fx.win_logic` | SNES window model |

Priority is 0 (front) … 3 (back) on every layer and sprite.

## Affine layers

```c
ppu16_mat6_t rowmat[240];                        /* one matrix per line   */
ppu16_mat6_t base = { .a = 1 << 16, .d = 1 << 16, .e = cx, .f = cy };
ppu16_affine_fold_rowmat(rowmat, 240, &base);    /* fold b*y, d*y once    */

ppu16_affine_t L = {
    .mip = { mip0, mip1, mip2, mip3 },           /* 256²…32², 8bpp        */
    .palette = pal,
    .rowmat = rowmat, .rowmat_count = 240,
    .dest_w = 320, .dest_h = 240,
    .mode = PPU16_AFF_QFA, .flags = PPU16_AF_WRAP,
};
ppu16_affine_claim(&ppu, 0, &L);
```

For perspective, fill the table directly with
`ppu16_affine_perspective_fill()` — one divide per scanline at setup, never
in the sampler. Per-line palettes (`palette_rows`) and per-line matrix
overrides are what power raster effects.

## Rendering

```c
ppu16_render_frame(&ppu, fb);                    /* whole frame           */
ppu16_render_line(&ppu, y, line);                /* scanout callback      */
```

## Formats

**Tileset** (8×8 tiles): 2bpp = 16 B/tile (2 px/byte, low 2 bits = left
pixel), 4bpp = 32 B/tile (low nibble = left pixel), 8bpp = 64 B/tile.
Colour index 0 is transparent.

**Map entry** (16-bit LE): bits 0–9 tile, 10–12 palette slot, 13 priority,
14 hflip, 15 vflip.

**Sprite tile layout**: tiles advance left→right then top→bottom.

`tools/asset_tool.py` converts PNGs into exactly these formats.

## Split operation (link protocol)

See `docs/protocol.md` — the same register model, serialised.

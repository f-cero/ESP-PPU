# PPU-16

**A 16-bit-class software PPU for ESP32 display offload.** Console architecture:
a register-file API, tile backgrounds, sprites, and a quarter-fill affine
(Mode 7-class) engine — implemented in portable C99, rendered by the "GPU"
device, and driven either locally or over an SPI link from a separate game CPU.

Think of it as a virtual console: a game engine targets the PPU-16 *register
model*, not a framebuffer. Whether those registers live on the same chip or on
a Cheap Yellow Display (CYD) acting as display + GPU over SPI is transparent
to game code.

```
   game CPU (main ESP32)                    GPU (CYD / S3 / P4)
  ┌────────────────────────┐   SPI link   ┌───────────────────────────┐
  │  game logic            │─────────────▶│  PPU-16 core (this lib)   │
  │  targets ppu16_* API   │  register    │  tile BG · sprites · QFA  │
  │  or link protocol      │  + memory    │  scanline compositor      │
  └────────────────────────┘  commands    └────────────┬──────────────┘
                                                       │ scanout
                                                       ▼
                                                 ILI9341 / RGB panel
```

## Feature set (16-bit class)

| Subsystem | Features |
|---|---|
| **Tile backgrounds** | 2/4/8 bpp 8×8 tiles, SNES-format 16-bit map entries (tile / palette / flips / priority), per-axis scroll, **HDMA-style per-scanline scroll tables** |
| **Sprites (OAM)** | 8–64 px, 4/8 bpp, H/V flip, per-sprite priority, **affine (rotating) sprites** via per-scanline incremental transform |
| **Affine layers** | SNES Mode 7-class: per-scanline matrix table, **QFA quarter-fill affine** (render 160×120, arithmetic-free 2× upscale), mip chain with per-row LOD selection, per-scanline perspective floors, alpha-key |
| **Raster effects** | Per-scanline palette rows, per-line matrix overrides, wobble/water/barrel-roll (all just different row-matrix tables), windows, mosaic, brightness fade |
| **Colour math** | RGB565 saturating add / subtract / half modes with per-layer enable masks, backdrop colour |
| **Platform** | Hardware-agnostic core, capability **autodetection**, SPI **link protocol** with CRC, host build with unit tests in CI |

## Design rules (enforced in code review and AGENTS.md)

1. **No per-pixel multiply** anywhere in the samplers — matrices are evaluated
   once per scanline and incremented.
2. **The upscale stage contains no arithmetic** — the QFA doubling pass is
   SWAR stores and row copies only.
3. **No floats in inner loops.** All math is signed 16.16 fixed point.
4. **No heap in the core.** The caller provides memory; the PPU returns at
   init time.
5. **Expose registers, not a framebuffer.** Every game inherits the same look,
   the same performance profile, and the same shader-free aesthetic.

## Layout

```
include/ppu16/   public API (C99, heavily documented)
src/             the PPU core — portable, no hardware assumptions
ports/esp32/     ESP32-S3 / CYD notes + IDF component wiring
tests/           host unit tests (also run under ASan/UBSan in CI)
examples/host/   Mode 7 demo that writes PPM frames
tools/           asset pipeline (PNG → tiles/palettes/maps/mips)
docs/            architecture, API, protocol, porting, performance, roadmap
```

## Quick start (host)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build
./build/mode7_demo          # writes demo_*.ppm frames
```

## Quick start (targeting it as a platform)

Local (game and PPU on one chip):

```c
#include "ppu16/ppu16.h"

static ppu16_t ppu;                       /* ~4 KB — static, not heap */

ppu16_config_t cfg = {
    .width = 320, .height = 240,
    .mode   = PPU16_MODE_AFFINE,          /* 1 QFA layer + 1 tile BG   */
};
static uint8_t arena[PPU16_ARENA_MAX];
ppu16_init(&ppu, &cfg, arena, sizeof(arena));

ppu16_bg_claim(&ppu, 0, &my_bg);          /* register tile BG 0        */
ppu16_affine_claim(&ppu, 0, &my_layer);   /* register affine layer 0   */

/* per frame: update scroll / OAM / row matrices, then */
ppu16_render_frame(&ppu, framebuffer);    /* or render_line() per scanout */
```

Split (game CPU → CYD GPU over SPI): see `docs/protocol.md`. Game code writes
the *same* register model; the link layer serialises it as CRC-checked
packets (`MEM_WRITE`, `REG_WRITE`, `FRAME`).

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).


## MVID tile video

Full-screen animation without a frame buffer: a clip is a tile atlas, a
tilemap of SNES-packed entries and a palette — exactly what the PPU already
eats. Pans cost 16 bytes per frame, fades cost 0. The decoder
(`src/mvid_decode.c`, ~250 lines) is allocation-free C99; callers supply the
sheet/map buffers, or bind them straight to a BG layer for the zero-blit
path. See `docs/mvid.md` (normative spec) and `docs/mvid-handoff.md`
(integration guide, resource budget, authoring envelope). Reference clips
live in `assets/clips/`; the host test suite verifies both decode to their
published fingerprints. `tools/mvid_encode.c` is the host-only encoder for
authoring clips; `examples/host/mvid_demo.c` plays a clip to PPM.

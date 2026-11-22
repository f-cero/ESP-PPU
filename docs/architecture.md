# Architecture

## The console model

PPU-16 is deliberately shaped like a 16-bit console PPU, not like a
framebuffer library. Game engines write **registers** (layers, palettes, OAM,
per-scanline tables) and consume **scanlines**. Everything a 16-bit machine
could express per scanline — HDMA scroll, per-line palettes, per-line affine
matrices, windows, raster colour math — falls out of the same "resolve
per-line state, then render the line" structure for free.

```
                     per frame (game / scene setup)
        ┌────────────────────────────────────────────────────┐
        │ fill row-matrix tables, scroll tables, palettes,   │
        │ OAM, tilesets, mip chains                          │
        └────────────────────────────────────────────────────┘
                              │
                              ▼  per scanline (ppu16_render_line)
   ┌──────────┐   ┌───────────────────────┐   ┌───────────────────┐
   │ windows  │──▶│ collect drawables for  │──▶│ composite spans   │
   │ masks    │   │ the line, sort back-   │   │ (cmath, fade,     │
   │          │   │ to-front by priority   │   │ priority order)   │
   └──────────┘   └───────────────────────┘   └─────────┬─────────┘
                                                        ▼
                                              RGB565 line / frame
```

## Subsystems

| Module | Responsibility |
|---|---|
| `src/ppu.c` | lifecycle, scanline compositor, full-QFA fast path |
| `src/bg.c` | tile BG span renderer (2/4/8 bpp, flips, wrap, HDMA scroll) |
| `src/sprite.c` | OAM span renderer (plain + affine sprites) |
| `src/affine.c` | Mode 7-class layers: row-matrix tables, LOD, QFA sampling, SWAR doubling |
| `src/link.c` | packet framing, CRC-16, host encoder, GPU decoder |
| `src/link_gpu.c` | GPU-side dispatcher: memory writes, register file, OAM/matrices |
| `src/hal.c` | capability autodetection + mode recommendation |

## The affine engine (QFA)

The affine layer implements the QFA technique: render the layer at half
resolution in each axis (4x fewer sampled pixels), then double it on scanout
with a pass that performs **no arithmetic per output pixel** — one SWAR word
store per two output pixels, one row copy vertically. The samplers are pure
data consumers: they read a per-scanline matrix table (the SNES HDMA model)
and increment two accumulators per pixel. **No per-pixel multiply exists
anywhere in the pipeline** — that invariant is what makes Mode 7-class
graphics viable on a microcontroller with no blitter, and it is enforced by
AGENTS.md and code review.

Per-scanline effects (perspective floors, wobble, water, barrel rolls,
per-line palettes, letterboxing) are all just different ways to *fill the
row-matrix and palette tables* — the sampler never changes.

## The fast-path decision

`ppu16_render_frame` checks whether the current configuration is a single
opaque full-frame affine layer with no other drawables, windows, colour math
or mosaic. If so it takes the full QFA pipeline (sample quarter-res into an
arena scratch, SWAR-double to the frame). Otherwise it falls back to the
scanline compositor, which is correct for arbitrary layer stacks. The fast
path is the *performance* choice; the compositor is the *correctness*
baseline — both are always compiled and tested.

## Threading model

The core is single-threaded and lock-free by design: one thread (the render
core/task) drives `ppu16_render_line` / `ppu16_render_frame`. On dual-core
ESP32 targets, run audio on the other core — never share the render core
with audio. Scanout can consume the same framebuffer double-buffered;
tearing policy belongs to the port, not the core.

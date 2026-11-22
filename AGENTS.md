# AGENTS.md — guidance for agentic coding tools

Welcome. This file tells you how to work *in this repository* so your changes
ship and pass CI on the first try.

## What this project is

PPU-16 is a software PPU (picture processing unit) library in C99 that gives
16-bit-console-class graphics to ESP32-class microcontrollers, with the
renderer running either locally or on a separate "GPU" device (a CYD) driven
over an SPI link. Game engines target its register-file API as a platform.

## Non-negotiable invariants

These are the rules the whole design exists to protect. **Violating any of
them is a bug even if the tests pass.**

1. **No per-pixel multiply in samplers.** Matrix terms are evaluated once per
   scanline (row-matrix tables) and *incremented* per pixel (two 32-bit adds).
   The compiled affine sampler must disassemble with zero `mull`/`mul` in the
   inner loop.
2. **The QFA upscale stage performs no arithmetic per output pixel** — SWAR
   word stores (`p | (p << 16)`) and `memcpy` row copies only.
3. **No floats in inner loops.** All geometry is signed 16.16 fixed point
   (`int32_t`, `PPU16_FP16_ONE == 65536`). Use `int64_t` intermediates for
   products of 16.16 values.
4. **No heap in `src/`.** Callers provide memory (`ppu16_init` takes an arena;
   `ppu16_mem_required` tells them how much). `malloc` is allowed in tests,
   examples, and tools only.
5. **No hardware assumptions in the core.** `src/` must not touch registers,
   HALs, or include chains of any SoC. Everything hardware-specific goes
   through `include/ppu16/hal.h` or the link transport callbacks.
6. **Wrap is a mask, not a branch.** Map wrapping uses `& 0xFF`-style masks.
   Do not add modulo or bounds checks in hot paths.
7. **C99, functional style.** All state is passed explicitly; no globals, no
   hidden mutable state. Every non-trivial function carries a comment block
   explaining *why*, not restating *what*.

## Build & test (do this before declaring done)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j
ctest --test-dir build --output-on-failure
```

CI also builds with `-Werror` and runs the Debug build under
ASan + UBSan (`tests/test_main.c` harness). Your change must be clean under
both. If you touched the samplers, add or extend a test in `tests/`:

- geometry changes → `test_affine.c` (identity / QFA-vs-full-res / wrap)
- compositing changes → `test_render.c` (golden scanlines)
- protocol changes → `test_link.c` (host→gpu round trip)

## Repository map

| Path | What lives there |
|---|---|
| `include/ppu16/` | Public API. Changing a struct layout here changes the wire format too (`src/link_gpu.c` mirrors OAM/matrix layouts) — update both together. |
| `src/` | Portable PPU core: compositor (`ppu.c`), tile BG (`bg.c`), sprites (`sprite.c`), affine/QFA (`affine.c`), link protocol (`link*.c`), capability detection (`hal.c`). |
| `tests/` | Host unit tests; no framework, one `test_main.c` registry. |
| `examples/host/` | Runnable demos writing PPM frames. Keep compiling: they double as API documentation. |
| `tools/asset_tool.py` | PNG → tilesets/maps/palettes/mip chains. Tile formats are documented in `docs/api.md` §Formats. |
| `ports/esp32/` | SoC wiring, IDF component, panel notes. Nothing here is allowed to be included by `src/`. |
| `docs/` | `architecture.md`, `api.md`, `protocol.md`, `porting.md`, `performance.md`, `roadmap.md`. |

## Performance work

`docs/performance.md` is the law: every `[MODEL]` number in it is a
hypothesis until measured on target. When you optimise:

1. Add a benchmark scenario (the seven in `docs/performance.md` §Scenarios).
2. Record median **and max** frame time — max matters for frame pacing.
3. Quote the number in the commit message, never in the README until
   re-measured on hardware.

The bottleneck ordering is fixed by the spec: **destination (panel) bandwidth
first, map placement (SRAM) second, sampler cost last.** Do not micro-optimise
the sampler while the panel link is the wall.

## Protocol changes

The SPI link format (`docs/protocol.md`) is versioned (`PPU16_LINK_VERSION`).
Bumping it requires: updating `src/link.c` + `src/link_gpu.c` + the doc in
the same commit, and a round-trip test in `test_link.c`. Never send struct
dumps over the wire — all wire formats are explicitly packed little-endian
fields converted field-by-field.

## Commits

Small, single-purpose commits; imperative mood
(`qfa: clamp perspective step to 8.0`). If a commit changes behaviour,
it changes a test or the docs in the same commit.

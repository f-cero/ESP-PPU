# Porting guide

The core in `src/` compiles anywhere C99 compiles. A port answers three
questions: **where does the memory go**, **what can the chip do**, and **how
do frames reach the panel**.

## 1. Memory placement — the biggest decision

The affine map (and mip chain, ~87 KB) must live in **internal SRAM**; the
destination can live anywhere, including PSRAM. An adversarial-stride
PSRAM fetch pattern (fast forward motion + rotation — exactly the racing
camera you care about) costs 10x more than an SRAM-resident map. See
`performance.md` §Memory placement for the numbers, and record per target
which placement ships.

Sprite/tile memory follows the same rule: hot tiles in SRAM; the frame in
PSRAM is fine.

## 2. Capability detection

Override `ppu16_hal_detect()` (weak in `src/hal.c`) with SoC probing:

| Field | ESP32 | ESP32-S3 | ESP32-P4 |
|---|---|---|---|
| `cpu_hz` | 240 M | 240 M | 360 M (RISC-V, HW divide) |
| `sram_bytes` | ~520 KB | ~512 KB | ~768 KB |
| `psram_bytes` | 0 | 8 MB | 32 MB |
| `panel_bps` | SPI 40 M → 32 fps wall | SPI / parallel | MIPI-DSI / RGB |
| `has_ppa` | 0 | 0 | 1 |

`ppu16_caps_recommend_mode()` turns that into a display mode. The panel
link is the wall that outranks everything: a 4-wire SPI ILI9341 at 40 MHz
cannot carry 320×240@60 (32 fps ceiling). The stock CYD is a **development
target, not a shipping target** — use a reduced active window (256×224,
SNES-native) on SPI, or a parallel-RGB panel for production.

## 3. Scanout

The core produces RGB565 lines/frames; the port decides the plumbing:

* **Parallel RGB / MIPI-DSI (P4 class):** full framebuffer in PSRAM, LCD_CAM
  or DSI DMA scans it out continuously; the render core writes the back
  buffer only. On PPA-capable targets, replace the QFA SWAR doubling with a
  single PPA scale descriptor (V3) and double-buffer it.
* **SPI panels:** stream from a line/strip buffer with DMA; never write the
  full 153 KB twice. Prefer a reduced active window.

## 4. ESP32 / CYD wiring notes (`ports/esp32/`)

* The CYD (ESP32-32E + ILI9341 over 4-wire SPI) runs the **GPU side**:
  `ppu16_link_gpu_t` + decoder fed from the SPI slave path, layer claims
  pointing into the GPU pools, then `ppu16_render_frame` + panel write.
* The main ESP32 runs the **host side**: `ppu16_link_host_t` with a sink
  that pushes bytes over SPI master.
* Audio gets its own core, unconditionally. A shared render/audio core
  produces dropouts that look like render bugs for weeks.
* The frame limiter must be a budget dispatcher (fixed 16.67 ms deadline,
  per-subsystem caps), never an uncapped loop and never a bare sleep.

## 5. Acceptance criteria (from the spec)

1. Two QFA affine layers at 60 fps with p95 ≤ 12 ms / max ≤ 16 ms on the
   render core (perspective-floor scenario).
2. One affine layer under the adversarial scenario at ≤ 4 ms.
3. No per-pixel multiply in the compiled sampler — **verify by disassembly**.
4. Map in internal SRAM on every shipping target.
5. Upscale contains no per-pixel arithmetic.
6. ASan clean on the host build (CI enforces this).
7. Per-scanline palette demonstrated with a raster gradient.

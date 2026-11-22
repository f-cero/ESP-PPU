# ESP32 port notes (S3 / CYD / P4)

This directory holds port glue; nothing here may be included by `src/`.

## Repository as an IDF component

Add this repo under `components/ppu16` (or set `EXTRA_COMPONENT_DIRS`),
then create `idf_component/CMakeLists.txt`-style registration:

```cmake
idf_component_register(
    SRCS "src/ppu.c" "src/bg.c" "src/affine.c" "src/sprite.c"
         "src/link.c" "src/link_gpu.c" "src/hal_port.c"
    INCLUDE_DIRS "include")
```

Provide `hal_port.c` with your own `ppu16_hal_detect()` (it overrides the
weak default in `src/hal.c`) reporting CPU clock, SRAM/PSRAM budget, panel
link bandwidth and PPA presence.

## Memory placement (the decision that outranks code)

* Affine map + mip chain (~87 KB at LOD0-3): **internal SRAM**, always.
* Framebuffer: PSRAM is fine (P4/S3); on the CYD there is no PSRAM — keep
  the active window reduced (256x224) and stream line buffers over SPI.
* QFA scratch: 38.4 KB SRAM at 320x240.

## CYD as GPU (SPI link)

The CYD board (ESP32-32E + ILI9341, 4-wire SPI) is the display + PPU:

1. SPI slave or UART transport feeds `ppu16_link_decode()`.
2. `ppu16_link_gpu_t` pools are the GPU's own tile/map/palette memory.
3. Claim layers pointing into the pools, then per PRESENT run
   `ppu16_render_frame()` and push the frame with the panel driver.
4. Stock panel at 40 MHz caps 320x240 at ~32 fps — dev target only.
   For 60 fps use parallel RGB (S3 with LCD_CAM / P4 with MIPI-DSI).

## Core allocation

* Render core: PPU + game render. Never share with audio.
* Audio core: dedicated, unconditionally.
* Frame pacing: budget dispatcher with hard per-subsystem caps, not a sleep.

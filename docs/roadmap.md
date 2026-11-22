# Roadmap

## Shipped
- Tile BGs (2/4/8bpp, flips, wrap, HDMA scroll tables), sprite engine
  (plain + affine), affine layers with row-matrix tables, LOD selection,
  QFA sampling + SWAR doubling, colour math, windows, mosaic, brightness,
  per-scanline palettes, link protocol + GPU dispatcher, capability
  autodetection, host test suite in CI.

## Next (in spec order)
- [ ] Per-entry BG priority in the compositor (the map bit is parsed but
      the layer priority currently wins — make per-tile priority compose).
- [ ] 2bpp/4bpp/8bpp asset pipeline round-trip in `tools/` (PNG in, C
      arrays out).
- [ ] Benchmark harness target for ESP-IDF (`IDF_TARGET=linux` host build
      plus on-target scenarios 1–7).
- [ ] PPA (V3) port for ESP32-P4: single-scale descriptor, double buffered.
- [ ] SPI slave transport binding for the CYD GPU side.
- [ ] Window inversion matrix tests (the four logic modes × two windows).
- [ ] Disassembly check in CI: assert zero multiplies in the affine
      sampler inner loop (acceptance criterion 3).
- [ ] Sprite priority: per-sprite-vs-per-tile composition test.
- [ ] docs: worked example — a Mode 7 racer scene graph → register writes.

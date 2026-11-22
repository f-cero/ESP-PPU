# Performance model

Numbers marked `[MODEL]` are hypotheses from a cycle-cost model and **must
be replaced with measurement** before being quoted anywhere.

## Budget at 60 Hz (16.67 ms)

| Resource | Budget | Allocation |
|---|---|---|
| Core 0 — render | 10.0 ms | 2 tile BG 2.5 · 2 affine layers (QFA) 2.2 · 64 sprites 2.5 · slack 2.8 |
| Core 1 — audio | 12.0 ms | dedicated, unconditionally |
| Panel link | — | constant: `w·h·2` bytes/frame, every frame |

## The wall: destination bandwidth

Output bytes per frame is **constant** — rendering cheaper does not help if
the panel link is the bottleneck.

| Panel interface | Effective | Ceiling @320×240 | Verdict |
|---|---|---|---|
| 4-wire SPI @ 26 MHz | 3.25 MB/s | 21 fps | unusable |
| 4-wire SPI @ 40 MHz (stock CYD) | 5 MB/s | 32 fps | hard wall |
| SPI @ 80 MHz (ILI9341 OC) | 10 MB/s | 65 fps | intermittent on some panels |
| Parallel RGB565 | 26–40 MB/s | 173–260 fps | the right answer |

On SPI: reduce the active window (256×224 SNES-native → ~43 fps) and stream
line buffers, never double-write the full framebuffer.

## Memory placement

| Map location | 76,800 px `[MODEL]` | 19,200 px `[MODEL]` |
|---|---|---|
| Internal SRAM | 1.6–2.6 ms | 0.4–0.7 ms |
| PSRAM, coherent stride | 2.5–4.5 ms | 0.6–1.1 ms |
| PSRAM, adversarial stride | **12–25 ms** | 3–6 ms |

**The map's location matters more than the render mode.** Adversarial stride
= high minification + rotation (fast racing camera) — benchmark under that
condition, not a slow rotation.

## QFA mode costs `[MODEL]`

| Variant | Sampled px | @240 MHz |
|---|---|---|
| V0 full-res sample | 76,800 | 2.6 ms |
| **V1 QFA + SWAR dup** | 19,200 | **1.1 ms** |
| V1H horizontal-only | 38,400 | 1.8 ms |
| V2 pre-doubled texture | 76,800 | 2.6 ms (better locality, cleaner image) |
| V3 PPA upscale (P4) | 19,200 | 0.7 ms |

## Benchmarks (run before quoting anything)

Measure `esp_timer_get_time()` around the sampler only and sampler+dup,
100 frames, report **median, p95 and max** (max matters for pacing), for
each scenario:

1. Static 1:1, no rotation — sanity floor
2. Slow rotation 1:1 — the least informative case
3. Fast rotation 45°, 1:1
4. Perspective floor, horizon at row 80 — LOD 0→3 in one draw
5. **High minification 8×, rotated 30° — the adversarial case**
6. High magnification 8×, no rotation
7. Scenario 5 with the map in SRAM vs PSRAM

Decision gate: do not pick a platform until you can state per target —
V1 ms/frame under scenarios 4 and 5, SRAM headroom after map + scratch +
mips, and whether two affine layers fit in 2.2 ms.

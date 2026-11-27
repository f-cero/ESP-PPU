# HANDOFF — MVID v1 (tile video) for the ESP-PPU project

**From:** Mosaic, commit `6e09777` (`main`), 2026-11-26.
**Subject:** a finished, tested video format that costs a tilemap instead of a
frame buffer, plus a 4-file portable decoder.
**Status:** implementation-complete and tested here; one ack outstanding (§7.1).

Mosaic needed full-screen animation on a PPU that owns 2 tile backgrounds, 1024
tiles and 2×256 palettes. Video as pixels is impossible at that budget; video as
*what the PPU already eats* — a tile atlas, a tilemap, a palette — fits. MVID v1
is that container. A pan across a still costs **16 bytes**. A fade costs **0
bytes**. The decoder is allocation-free C99 with no dependencies: the caller
brings three buffers, the decoder never calls `malloc`.

Everything in this package is standalone. Nothing needs Mosaic's engine, SDL, or
an ESP-IDF component. If ESP-PPU wants video, this is the smallest path to it.

---

## 1. What is in the package

| Path | What it is | Take it if |
|---|---|---|
| `HANDOFF-esp-ppu-mvid.md` | this document | always |
| `codec/mvid.h`, `mvid_util.c`, `mvid_decode.c` | decoder, 471 lines with header | you want MVID playback |
| `codec/mvid_encode.c` | encoder (median cut, INTRA/PATCH/PAN planner) | you want to author clips |
| `docs/10-fmv.md` | normative spec, §10 (format) and §10.6 (web/API) | always |
| `web/mvid.js` | JS decoder + `<mosaic-video>` + Player, 504 lines | browser docs/demos |
| `standalone/demo_mvid.c`, `verify.sh` | proof the codec builds alone | first thing to run |
| `players/mvidplay-{linux,win32}.c` | reference hosts (SDL2 / GDI) | a desktop player |
| `clips/splash.mvid` | 5-shot intro, 70 frames, 245.5 KB | a real test clip |
| `clips/title.mvid` | single INTRA frame, 16.3 KB | smallest possible test |
| `clips/splash.mvj` | the edit-decision list that produced it | understanding authoring |
| `page/splash.html`, `page/title.html` | one-file HTML players, decoder inlined | sharing a clip |
| `evidence/*.png` | rendered frames + contact sheet | eyeballing quality |

Measured sizes and hashes of every file: `SHA256SUMS.txt`.

---

## 2. The format in one page

### Header — 56 bytes, little-endian

| off | type | field |
|---|---|---|
| 0 | `char[4]` | `"MVID"` |
| 4 | `u16` | version (`1`) |
| 6 | `u16` | header length (`56`) |
| 8 | `u16` | width — coded size, 320 or 160 |
| 10 | `u16` | height — 240 or 120 |
| 12 | `u8` | fps |
| 13 | `u8` | flags: `1` loop, `2` audio (reserved), `4` QFA (coded 160×120, doubled) |
| 14 | `u16` | frame count |
| 16 | `u16` | map_w, map_h — **logical** tilemap dims in tiles |
| 20 | `u8` | bpp (4 or 8) |
| 21 | `u16` | palette entry count |
| 23 | `u32` | palette offset (RGB565, `count × 2`) |
| 27 | `u32` | frame index offset (`frames × 16`) |
| 31 | `u32` | audio offset (0 when absent) |
| 35 | `u32` | audio length |
| 39 | `u32` | sheet bytes (`tiles × bpp/8 × 64`) |
| 43 | `u32` | CRC32 of everything after the header |

The atlas and the tilemap follow the index, in that order. The map is padded up
to the next power of two on each axis and wrap-filled, so the decoder's per-pixel
modulo is a mask.

### Frame record — 16 bytes

| off | type | field |
|---|---|---|
| 0 | `u8` | type: 0 INTRA, 1 PATCH, 2 PAN |
| 1 | `u8` | flags (reserved) |
| 2 | `u16` | payload length |
| 4 | `i16` | scroll_x |
| 6 | `i16` | scroll_y |
| 8 | `u16` | op count |
| 10 | `u16` | tile count |
| 12 | `u8` | brightness 0..16 (16 = unchanged) |

### Payload grammar

```
u16 np;  np × { u8 palette_index, u16 rgb565 }        palette writes
u16 nt;  nt × { u16 slot, bpp/8×64 bytes }            atlas writes
u16 no;  no × { u16 map_cell, u16 entry }             map writes
```
An INTRA carries a full atlas and full map (resets state). A PATCH carries only
diffs. A **PAN carries an empty payload** — the 16-byte record *is* the frame,
and its scroll fields do the work.

Map entries are SNES-packed: bits 0-9 tile, 10-12 palette slot, 13 priority,
14/15 h/v flip. That is why a decoder is 249 lines: the PPU vocabulary is
already the format.

### Exactness rule (the only place implementations drift)

`scroll` is applied **per output pixel**, modulo the **logical** dims, exactly
once:

```
sx = (x + scroll_x) mod (map_w * 8)
sy = (y + scroll_y) mod (map_h * 8)
entry = map[(sy >> 3) * padded_mw + (sx >> 3)]
```

Mosaic shipped a double-apply bug once by folding affine scroll into the
rasteriser *and* the blit. Do it in one place.

4bpp packing: low nibble is the **left** pixel (`(px & 1) ? hi : lo`).

Brightness is applied at blit: `mvid_scale565(c, b)` = each channel
`(ch * b) / 16` in integer maths. `b == 0` is black, `b == 16` is identity.

---

## 3. Resource budget on target

Worst case, if you size buffers from the constants:

| buffer | formula | worst case | splash actual | title actual |
|---|---|---|---|---|
| atlas | `1024 × 64` | 64 KiB | **32 KiB** | 32 KiB |
| tilemap | `128² × 2` | 32 KiB | 4 KiB (64×32 padded) | 4 KiB (64×32) |
| palette | `256 × 2` | 512 B | 20 B (10 entries) | 224 B (112) |
| decoder state | `mvid_t` | ~120 B | — | — |
| framebuffer | `320 × 240 × 2` **not needed** if you blit into the PPU | 0 | — | — |

The last row is the point: `mvid_draw` can write straight into the framebuffer
you already have, or you can skip it and bind `v.sheet` / `v.map` to a BG layer
and let the PPU render — that is what Mosaic's engine does for splash screens.

Decode cost, measured on x86-64 and in node 22 (not on an ESP32 — see §8):
0.59 ms/frame for a 320×240 4bpp clip in JS; the C decoder is faster and
allocation-free. A 12 fps clip is ~0.6 % of one core at 100 MHz.

---

## 4. Clips in this package

| clip | frames | type mix | bytes | B/frame | SHA-256 (first 16) |
|---|---|---|---|---|---|
| `splash.mvid` | 70 | 7 INTRA / 25 PATCH / 38 PAN | 251,403 | 3,591 | `8ab14ebe4a89406a` |
| `title.mvid` | 1 | 1 INTRA | 16,666 | 16,666 | `2c03adc96cfd937c` |

`splash.mvid` is 5 Ken-Burns shots over painted backgrounds at 12 fps — 38 of its
70 frames cost 16 bytes each. That ratio (54 % of frames free) is the whole
argument for a pan-capable format.

Deterministic decode fingerprints (FNV-1a 64 over all RGB565 frames):

```
splash.mvid  fnv=e9f487cd54b107b9
title.mvid   fnv=5ff0b32bda700f9d
```

Any port that reproduces these two values on these two clips is decoder-exact.

---

## 5. What it costs to author a clip (measured envelope)

24-frame Ken-Burns from a 1200×900 photograph, 12 fps, compared against the
320×240 source:

| mode | B/frame | PSNR | verdict |
|---|---|---|---|
| tiles, 4bpp (`tile4`) | 5,618 | **7.85 dB** | photographic content: unusable |
| QFA, 4bpp (`qfa4`) | 4,617 | **11.50 dB** | poor; palette is the bottleneck |
| QFA, 8bpp (`qfa8`) | 18,761 | **19.05 dB** | acceptable for a splash |
| pixel art, 4bpp | — | **bit-exact** | the format's home turf |

Two honest consequences:

1. **MVID is not a video codec for photographs at full resolution.** A 320×240
   photo needs ~1200 distinct tiles per frame; the ceiling is 1024, so the
   encoder must merge. 4bpp on photos is the weak spot: 16 colours per slot,
   median-cut ordering. k-means slot clustering is the planned fix; it is not in
   this package.
2. **For pixel art it is lossless** — same pixels in, same pixels out — because
   the source *is* tiles.

QFA with 8bpp is the recommended default for photographic splash content;
4bpp is the recommended default for anything drawn by hand.

---

## 6. Integration checklist for ESP-PPU

Ordered, each step independently verifiable.

1. **Vendor three files.** Copy `codec/mvid.h`, `mvid_util.c`, `mvid_decode.c`
   into your tree. They need `<stdint.h>`, `<stddef.h>`, `<string.h>` and
   `<stdio.h>` (the latter only for `mvid_read_file`; drop that function if you
   have no filesystem).
2. **Validate at load.** `mvid_check(data, size)` walks the header, palette,
   index, every frame header and every payload bound. It returns `0` or a
   negative code; `mvid_err_str(e)` gives text. Reject the resource on non-zero —
   this is the only place untrusted bytes are parsed.
3. **Allocate once.** `mvid_parse(&info, data, size)` then
   `mvid_sheet_size(&info)` / `mvid_map_entries(&info)` give the two buffer
   sizes. Static buffers from `MVID_MAX_TILES` / `MVID_MAX_MAP` are the simple
   answer (96 KiB worst case); size to the clip if RAM is tight.
4. **Open.** `mvid_open(&v, data, size, sheet, map)` decodes frame 0.
5. **Per frame.** `mvid_draw(&v, fb, w, h)` → RGB565, brightness applied. Then
   `mvid_next(&v)` (returns 0 at the end). Pace from `v.info.fps`; if
   `MVID_F_LOOP` is clear, stop and either hold the last frame or fade out.
6. **Or skip the blit.** Bind `v.sheet` and `v.map` to a BG and set the BG scroll
   from `v.scroll_x/y`, recomputed only when a PATCH/PAN/intra arrives. This is
   the zero-blit path and the reason the format exists.
7. **Teardown.** `mvid_close(&v)` clears struct state only; the buffers are
   yours and stay allocated.
8. **QFA clips.** `MVID_F_QFA` means coded 160×120 for a 320×240 display. Either
   let `mvid_draw` double it (nearest, free-ish) or pass it at native size to
   your existing line-doubling stage — but then all scroll maths runs in display
   space.

---

## 7. Open points that need a decision from ESP-PPU

### 7.1 Ack spec-rev 2.1 (blocking freeze, not blocking use)

Spec-rev 2.1 moved two things into the payload:

- `bg_map` is now `u16 count | u16 w | u16 h | entries`, so map dims travel with
  the map instead of being implied by the visible size;
- `palette` is now `u16 count | u8 bank | u8 0 | count × rgb565`, so a clip can
  target palette bank 0 or 1 explicitly.

Both are already implemented in the C codec, the JS port and the encoder in this
package. If ESP-PPU has its own MVID writer, it must emit this form. Ack here and
the rev is frozen.

### 7.2 Limits

`MVID_MAX_TILES 1024`, `MVID_MAX_MAP 128` tiles/axis, `MVID_MAX_FRAMES 4096`.
These are compile-time constants, not header fields — a clip encoded under a
larger build will not load on a smaller one. If ESP-PPU's PPU has different tile
or map limits, say so and we align the constants before more clips are authored.

### 7.3 Audio — reserved, absent

`flags` bit 1 is reserved and always 0 in this package. There is no audio track,
no audio offset. If ESP-PPU wants scored video, the natural carrier is an SMF
stream (Mosa's synth is an SMF/wavetable player, 16 voices, **no PCM**), which is
~1 KB/minute instead of 1.3 MB. That is a format addition, not a decoder change.

### 7.4 Carriage

Mosaic carries MVID as cart kind `11` (`MS_A_MVID`), validated by
`ms_check_mvid()`, auto-discovered from `<dir>/splash.mvid`. ESP-PPU does not need
any of that: a length-prefixed MVID blob in whatever resource container exists is
enough. Do not port the cart layer.

### 7.5 Ownership of the QFA upscale

Core reserves the right to keep QFA doubling in the decoder so one clip plays on
both 160×120 and 320×240 targets. If ESP-PPU's PPU already line-doubles a BG
cheaply, prefer that and ignore the flag's implication, but do it in one place.

---

## 8. What is *not* here, and what was not verified

Not in the package, by design: Mosaic's engine, SDL Studio, the `mosaic` CLI, the
ESP32 port sources, the cart format. This is the codec and its evidence.

Verified on this machine:

- codec compiles standalone with `cc -O2 -std=c99 -Wall -Wextra` (no warnings),
  and both clips decode to the fingerprints in §4;
- C decoder and JS decoder produce **identical RGB565** for every frame of
  `splash.mvid`: 70/70 frames, 0 differing pixels of 5,376,000;
- generated HTML pages render in headless Chrome (both inspected visually);
- MVID clips play inside the Mosaic engine, in the SDL2 and Win32 players, and in
  the browser.

Not verified, stated plainly:

- **on-device ESP32 timing** — never run; only x86-64 and node measurements
  exist. Expect the decode to be dominated by the blit, not the parse;
- the Android `libmvid.so` was never built (no NDK in that sandbox);
- the Win32 player binary was cross-compiled but never executed (no wine);
- no interactive browser test (screenshot renders only);
- 4bpp on photographic content is 11.5 dB and known-poor; k-means is planned,
  not shipped.

---

## 9. Verify in five minutes

```sh
cd standalone
./verify.sh                  # builds demo_mvid from ../codec, checks both fingerprints
# expect: splash.mvid .. fnv=e9f487cd54b107b9
#         title.mvid  .. fnv=5ff0b32bda700f9d
```

Then open `page/title.html` in any browser — no server needed; the decoder and
the clip are inside the file. To re-encode a clip you need Mosaic's CLI
(`mosaic video encode`), which is not in this package.

---

## 10. Licence and provenance

Apache-2.0, `LICENSE` and `NOTICE` included. The encoder shells out to an
`ffmpeg` **binary** for decode/scale and links nothing from libav*, so the licence
stays clean for a static firmware build. Codec source is original work derived
from the ESP-16 reference *standards* (PPU-16, midicore), not a fork.

Upstream: `github.com/f-cero/Mosaic` @ `6e09777`. The `v1.1.0` release is
untouched by this work.

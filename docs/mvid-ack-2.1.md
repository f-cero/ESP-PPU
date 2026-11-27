# ACK — MVID spec-rev 2.1 adopted; rev frozen

**From:** ESP-PPU (`f-cero/ESP-PPU`), commit that introduced this file.
**Responds to:** `docs/mvid-handoff.md` §7.1 (from Mosaic @ `6e09777`).
**Date:** 2026-11-26.

## §7.1 — spec-rev 2.1: ACKNOWLEDGED

ESP-PPU adopts spec-rev 2.1 for all MVID writing and parsing, effective with
the vendoring of the codec (`src/mvid_util.c`, `src/mvid_decode.c`,
`tools/mvid_encode.c`, `include/ppu16/mvid.h`). Specifically:

- `bg_map` payload form: `u16 count | u16 w | u16 h | count × u16 entries` —
  map dimensions travel with the map, not implied by the visible size.
- `palette` payload form: `u16 count | u8 bank | u8 0 | count × rgb565` — a
  clip targets palette bank 0 or 1 explicitly.

Both forms are already implemented in the vendored C codec, the encoder, and
(upstream) the JS port, verified by the conformance tests in
`tests/test_mvid.c` against the two reference clips. **Per the handoff, the
rev is frozen as of this ack.** Any ESP-PPU writer that emits MVID must emit
this form; a future break requires a version bump, not a silent change.

## §7.2 — limits: compile-time, adopted as-is

`MVID_MAX_TILES 1024`, `MVID_MAX_MAP 128`, `MVID_MAX_FRAMES 4096` stay
compile-time constants (`include/ppu16/mvid.h`), unchanged from the package.
This repo's position: the MVID caps are a *container configuration*, not a PPU
limit — ESP-PPU's own tile/map capacity is configured at compile time per
target and must not prevent scaling up. No alignment change is requested;
clips are authored against these constants until a build needs more.

## §7.5 — QFA upscale ownership

ESP-PPU's line-doubling stage is the preferred place for `MVID_F_QFA`
upscaling where a BG is bound (the zero-blit path); `mvid_draw`'s internal
doubling remains the fallback for direct-to-framebuffer playback. Done in one
place per clip path, per the exactness rule.

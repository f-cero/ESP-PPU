# Link protocol

When the PPU runs on the GPU device (CYD-class board) and game code on the
main CPU, the *same register model* travels over the wire. Game code never
learns about the transport: the host encoder mirrors the register API.

## Framing

```
+------+------+---------+-----------------+----------+
| 0xA5 | cmd  | len u16 | payload (len B) | crc16 u16|
+------+------+---------+-----------------+----------+
        \______ CRC-16/CCITT-FALSE over cmd+len+payload ____/
```

* Multi-byte fields are little-endian.
* `len` ≤ `PPU16_LINK_PAYLOAD_MAX` (512).
* The decoder resynchronises on the sync byte, so garbage between packets
  is survivable; a bad CRC silently drops the packet.

## Commands

| Cmd | Dir | Payload |
|---|---|---|
| `HELLO` 0x01 | host→gpu | `"PPU16"` + protocol version byte |
| `CAPS` 0x02 | gpu→host | capability report (hal.h layout) |
| `RESET` 0x03 | host→gpu | — |
| `ECHO` 0x04 | both | round-trip test |
| `MEM_WRITE` 0x10 | host→gpu | `u32 addr, u32 n, n bytes` into the GPU address space |
| `REG_WRITE` 0x11 | host→gpu | `([u16 reg][u32 val])*`; window regs carry `u16 x0,y0,x1,y1, u8 en,inv` |
| `OAM_WRITE` 0x12 | host→gpu | `u16 count`, packed 12-byte entries |
| `MAT_WRITE` 0x13 | host→gpu | `u16 count`, packed 16-byte 2×2 matrices |
| `FRAME` 0x14 | host→gpu | begin frame — following writes apply to it |
| `PRESENT` 0x15 | host→gpu | end frame — scanout |

## GPU address space (MEM_WRITE)

| Region | Base |
|---|---|
| BG tileset | `PPU16_AS_BG_TILES` 0x00000000 |
| Sprite tileset | `PPU16_AS_SPR_TILES` 0x00010000 |
| Tile maps | `PPU16_AS_MAP` 0x00020000 |
| BG palette | `PPU16_AS_PAL_BG` 0x00030000 (512 B) |
| Sprite palette | `PPU16_AS_PAL_SPR` 0x00030200 (512 B) |
| Row-matrix tables | `PPU16_AS_ROWMAT0(i)` 0x00031000 + i·0x1000 |

## Register file (REG_WRITE)

See `include/ppu16/link.h` for the full list — every entry maps 1:1 onto a
`ppu16_t` field. Unknown registers and commands are ignored
(forward-compatible), which is how minor protocol revisions stay
interoperable.

## Wire formats

All wire structures are **packed, little-endian, converted field-by-field** —
never struct dumps (host and GPU may differ in padding):

```
OAM entry (12 B): x s16, y s16, w u8, h u8, tile u8, pal u8,
                  bpp u8, priority u8, flags u8, affine_id s8
Matrix   (16 B): a s32, b s32, c s32, d s32      (16.16)
mat6     (24 B): a,b,c,d,e,f                     (16.16 each)
```

## Versioning

`PPU16_LINK_VERSION` bumps on any layout change. `HELLO` carries it; a GPU
that sees a version it does not speak should answer with its own `CAPS` and
a version-mismatch flag (port responsibility).

#!/usr/bin/env python3
"""PPU-16 asset pipeline: PNG -> tileset + map + palette (+ mip chain).

Emits the exact formats in docs/api.md:

  4bpp/8bpp tiles, 8x8, colour index 0 transparent, low nibble = left pixel
  SNES-style 16-bit map entries: tile 0-9, palette slot 10-12,
                                 hflip 14, vflip 15

Usage:
  python3 asset_tool.py convert input.png --bpp 4 --out assets/
  python3 asset_tool.py mips map256.bin --out mips/      # affine mip chain

The tool is deliberately dependency-light: PNG loading uses Pillow if
available, else a plain PPM/PNG8 fallback is NOT provided — convert your
art to PNG8 with indexed colour first.
"""
import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required: pip3 install pillow")

def downsample(src: bytes, size: int) -> bytes:
    """One box-filter step: 2x2 average of palette indices."""
    n = size // 2
    return bytes(
        (src[(2 * v) * size + 2 * u] + src[(2 * v) * size + 2 * u + 1]) // 2
        for v in range(n) for u in range(n)
    )

def cmd_convert(args):
    img = Image.open(args.input).convert("P")
    w, h = img.size
    if w % 8 or h % 8:
        sys.exit(f"image must be a multiple of 8: {w}x{h}")
    px = img.load()
    pal_in = img.getpalette()
    if pal_in is None:
        sys.exit("not an indexed image")

    # Our 565 palette (256 entries, BG bank).
    pal565 = []
    for i in range(256):
        r, g, b = pal_in[i * 3:(i + 1) * 3] if i * 3 + 2 < len(pal_in) else (0, 0, 0)
        pal565.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

    # Map: one entry per 8x8 block; tiles deduped.
    tiles = {}
    tile_list = []
    map_entries = []
    tw, th = w // 8, h // 8
    for ty in range(th):
        for tx in range(tw):
            data = bytearray(64 if args.bpp == 8 else 32)
            for fy in range(8):
                for fx in range(8):
                    idx = px[tx * 8 + fx, ty * 8 + fy]
                    o = fy * (8 if args.bpp == 8 else 4) + (fx >> 1 if args.bpp != 8 else fx)
                    if args.bpp == 8:
                        data[o] = idx
                    else:
                        data[o] |= (idx & 0xF) if (fx & 1) == 0 else ((idx & 0xF) << 4)
            key = bytes(data)
            if key not in tiles:
                tiles[key] = len(tile_list)
                tile_list.append(key)
            map_entries.append(tiles[key])

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "tileset.bin", "wb") as f:
        f.write(b"".join(tile_list))
    with open(out / "map.bin", "wb") as f:
        for e in map_entries:
            f.write(struct.pack("<H", e & 0x3FF))
    with open(out / "palette.bin", "wb") as f:
        for c in pal565:
            f.write(struct.pack("<H", c))
    print(f"wrote tileset.bin ({len(tile_list)} tiles), "
          f"map.bin ({tw}x{th}), palette.bin to {out}/")

def cmd_mips(args):
    src = Path(args.input).read_bytes()
    size = int(len(src) ** 0.5)
    if size * size != len(src):
        sys.exit("map must be square")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cur, s = src, size
    level = 0
    while s >= 32 and level < 4:
        (out / f"mip{level}.bin").write_bytes(cur)
        print(f"mip{level}: {s}x{s} -> {out}/mip{level}.bin")
        cur = downsample(cur, s)
        s //= 2
        level += 1

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("convert", help="PNG -> tiles/map/palette")
    c.add_argument("input")
    c.add_argument("--bpp", type=int, choices=[4, 8], default=4)
    c.add_argument("--out", default="assets")
    c.set_defaults(fn=cmd_convert)

    m = sub.add_parser("mips", help="8bpp map -> mip chain")
    m.add_argument("input")
    m.add_argument("--out", default="mips")
    m.set_defaults(fn=cmd_mips)

    args = ap.parse_args()
    args.fn(args)

if __name__ == "__main__":
    main()

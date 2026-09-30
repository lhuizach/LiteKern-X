#!/usr/bin/env python3
"""LiteKern X - build step: a wallpaper PNG -> the kernel's packed format.

Usage: python3 tools/wallpaper-pack.py WALLPAPER.png OUT.lkxw

The kernel has no PNG decoder and can't hold a raw 1024x600 image (2.4 MB,
over its 448 KiB limit), so the image is packed the way PNG packs it, only
simpler: each row of RGB bytes gets the PNG filter that suits it best, and
the lot is deflated. The kernel inflates and unfilters it (kernel/inflate.c,
kernel/wallpaper.c) and checks the checksum. Rules: docs/ASSET-PROMPTS.md §8.

Format, little-endian:
  "LKXW", u16 width, u16 height, u32 raw length (height * (1 + 3 * width)),
  u32 packed length, u32 FNV-1a of the unfiltered RGB bytes,
  u32 top, bottom, left, right edge colours (0xRRGGBB, averages: for filling
  a screen bigger than the image), then the raw deflate stream.
Also writes OUT.txt: "bottom=RRGGBB" etc., for the tests.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lkx_png import read_rgba  # noqa: E402

W, H = 1024, 600
MAX_PACKED = 512 * 1024


def fail(msg):
    sys.exit(f"wallpaper-pack: {msg}")


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filtered(row, prev):
    """The row under each PNG filter (bpp 3)."""
    n = len(row)
    left = lambda i: row[i - 3] if i >= 3 else 0
    upleft = lambda i: prev[i - 3] if i >= 3 else 0
    return [
        bytes(row),
        bytes((row[i] - left(i)) & 255 for i in range(n)),
        bytes((row[i] - prev[i]) & 255 for i in range(n)),
        bytes((row[i] - ((left(i) + prev[i]) >> 1)) & 255 for i in range(n)),
        bytes((row[i] - paeth(left(i), prev[i], upleft(i))) & 255 for i in range(n)),
    ]


def cost(data):
    """PNG's usual heuristic: smallest sum of the bytes read as signed."""
    return sum(b if b < 128 else 256 - b for b in data)


def average(pixels):
    n = len(pixels)
    return tuple(round(sum(p[i] for p in pixels) / n) for i in range(3))


def hexrgb(c):
    return (c[0] << 16) | (c[1] << 8) | c[2]


def main():
    if len(sys.argv) != 3:
        fail("usage: wallpaper-pack.py WALLPAPER.png OUT.lkxw")
    src, out = sys.argv[1], sys.argv[2]
    w, h, px = read_rgba(src)
    if (w, h) != (W, H):
        fail(f"{src}: is {w}x{h}, must be {W}x{H} (docs/ASSET-PROMPTS.md §8)")
    if any(p[3] != 255 for row in px for p in row):
        fail(f"{src}: has transparency; wallpapers must be opaque")

    rgb_rows = [bytes(c for p in row for c in p[:3]) for row in px]
    raw, prev = bytearray(), bytes(3 * w)
    for row in rgb_rows:
        options = filtered(row, prev)
        best = min(range(5), key=lambda f: cost(options[f]))
        raw.append(best)
        raw += options[best]
        prev = row

    comp = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    packed = comp.compress(bytes(raw)) + comp.flush()
    if len(packed) > MAX_PACKED:
        fail(f"{src}: packs to {len(packed) // 1024} KB, over {MAX_PACKED // 1024} KB")

    fnv = 0x811C9DC5
    for row in rgb_rows:
        for b in row:
            fnv = ((fnv ^ b) * 0x01000193) & 0xFFFFFFFF

    edges = {
        "top": average(px[0]),
        "bottom": average(px[-1]),
        "left": average([row[0] for row in px]),
        "right": average([row[-1] for row in px]),
    }
    header = b"LKXW" + struct.pack("<HHIII4I", w, h, len(raw), len(packed), fnv,
                                  *(hexrgb(edges[k]) for k in ("top", "bottom", "left", "right")))
    with open(out, "wb") as f:
        f.write(header + packed)
    with open(out + ".txt", "w", encoding="utf-8", newline="\n") as f:
        for k, c in edges.items():
            f.write(f"{k}={hexrgb(c):06x}\n")
    print(f"wallpaper-pack: {src} -> {out} ({len(packed) // 1024} KB packed, "
          f"{len(raw) // 1024} KB raw)")


if __name__ == "__main__":
    main()

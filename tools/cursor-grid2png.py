#!/usr/bin/env python3
"""LiteKern X - turn a cursor drawn as a text grid into a 32x32 RGBA PNG.

The grid is 32 lines of exactly 32 characters (other lines are ignored, so
you can paste an AI's whole answer), plus a line "hotspot: X,Y":

    .   transparent
    K   black body          #000000, opaque
    W   white edge          #FFFFFF, opaque
    w   soft white edge     #FFFFFF, half transparent (smooths curves)
    k   soft black          #000000, half transparent

Usage: python3 tools/cursor-grid2png.py GRID.txt assets/cursors/arrow.png
Checks the size, the characters, and that the hotspot is an opaque pixel,
and prints the cursors.json entry to use.
"""
import re
import struct
import sys
import zlib

SIZE = 32
PIXELS = {
    ".": (0, 0, 0, 0),
    "K": (0, 0, 0, 255),
    "W": (255, 255, 255, 255),
    "w": (255, 255, 255, 128),
    "k": (0, 0, 0, 128),
}


def fail(msg):
    sys.exit(f"cursor-grid2png: {msg}")


def parse(text):
    rows, hotspot = [], None
    for line in text.splitlines():
        m = re.match(r"\s*hotspot\s*:\s*(\d+)\s*,\s*(\d+)", line, re.I)
        if m:
            hotspot = (int(m.group(1)), int(m.group(2)))
            continue
        s = line.strip().strip("`")
        if s and set(s) <= set(PIXELS):
            rows.append(s)
    if len(rows) != SIZE:
        fail(f"expected {SIZE} grid rows, found {len(rows)}")
    for i, r in enumerate(rows):
        if len(r) != SIZE:
            fail(f"row {i} has {len(r)} characters, expected {SIZE}")
    if hotspot is None:
        fail('no "hotspot: X,Y" line')
    x, y = hotspot
    if not (0 <= x < SIZE and 0 <= y < SIZE):
        fail(f"hotspot {x},{y} is outside 0..{SIZE - 1}")
    if PIXELS[rows[y][x]][3] != 255:
        fail(f"hotspot {x},{y} is on '{rows[y][x]}', not an opaque pixel")
    return rows, hotspot


def write_png(path, rows):
    raw = b"".join(b"\0" + b"".join(bytes(PIXELS[c]) for c in r) for r in rows)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    if len(sys.argv) != 3:
        fail("usage: cursor-grid2png.py GRID.txt OUT.png")
    rows, (x, y) = parse(open(sys.argv[1], encoding="utf-8").read())
    write_png(sys.argv[2], rows)
    name = re.sub(r"\.png$", "", sys.argv[2].replace("\\", "/").split("/")[-1])
    print(f"wrote {sys.argv[2]} (32x32 RGBA). cursors.json entry:")
    print(f'    "{name}": {{ "file": "{name}.png", "hotspot": [{x}, {y}] }}')


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""LiteKern X - render the default arrow cursor as a 32x32 RGBA PNG.

The arrow is drawn as a vector shape at 16x size, its corners rounded, given
a 1px white edge, then scaled down so the edges are smoothly antialiased.

Usage: python3 tools/cursor-arrow.py assets/cursors/arrow.png [PREVIEW.png]
The optional preview shows the cursor 8x zoomed and at actual size on blue.
cursors.json entry: "arrow": { "file": "arrow.png", "hotspot": [1, 1] }
"""
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 32
ZOOM = 16
# Black body outline, in cursor pixels: tip, down the left edge, into the
# notch, round the tail, back up to the notch and out to the head's corner.
BODY = [(2.0, 1.6), (2.0, 18.4), (6.0, 14.9), (9.2, 21.0),
        (11.9, 19.8), (8.7, 13.9), (13.6, 13.9)]


def grow(im, px):
    return im.filter(ImageFilter.MaxFilter(int(px * ZOOM) * 2 + 1))


def shrink(im, px):
    return im.filter(ImageFilter.MinFilter(int(px * ZOOM) * 2 + 1))


def render():
    n = SIZE * ZOOM
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).polygon([(x * ZOOM, y * ZOOM) for x, y in BODY], fill=255)
    black = grow(shrink(mask, 0.45), 0.45)   # round the body's corners
    white = grow(black, 1.0)                 # 1px white edge all round
    b = black.resize((SIZE, SIZE), Image.BOX)
    w = white.resize((SIZE, SIZE), Image.BOX)
    out = Image.new("RGBA", (SIZE, SIZE))
    for y in range(SIZE):
        for x in range(SIZE):
            a, k = w.getpixel((x, y)), b.getpixel((x, y))
            v = round(255 * (a - k) / a) if a else 0
            out.putpixel((x, y), (v, v, v, a))
    return out


def preview(cur, path, bg=(90, 120, 160, 255)):
    tile = Image.new("RGBA", cur.size, bg)
    tile.alpha_composite(cur)
    big = tile.resize((SIZE * 8, SIZE * 8), Image.NEAREST)
    sheet = Image.new("RGBA", (big.width + SIZE + 24, big.height), bg)
    sheet.paste(big, (0, 0))
    sheet.paste(tile, (big.width + 16, 8))
    sheet.save(path)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__.strip().splitlines()[-3])
    cur = render()
    cur.save(sys.argv[1])
    print(f"wrote {sys.argv[1]} ({SIZE}x{SIZE} RGBA)")
    if len(sys.argv) == 3:
        preview(cur, sys.argv[2])


if __name__ == "__main__":
    main()

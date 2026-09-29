#!/usr/bin/env python3
"""LiteKern X - check a cursor PNG against docs/ASSET-PROMPTS.md §3 and make a preview.

Usage: python3 tools/cursor-check.py assets/cursors/arrow.png [PREVIEW.png]

Prints the checks and the likely hotspot. The preview shows the cursor at 1x
and 8x on the three backgrounds it must read on: navy, white and sky.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lkx_png import read_rgba, write_rgba  # noqa: E402

BACKGROUNDS = [(0x1e, 0x3a, 0x5f), (0xff, 0xff, 0xff), (0x48, 0xa6, 0xe8)]
ZOOM = 8


def blend(fg, bg):
    a = fg[3]
    return tuple((fg[i] * a + bg[i] * (255 - a)) // 255 for i in range(3)) + (255,)


def main():
    path = sys.argv[1]
    w, h, px = read_rgba(path)
    ok = True

    def check(cond, what):
        nonlocal ok
        print(f"{'ok  ' if cond else 'FAIL'} {what}")
        ok &= cond

    check((w, h) == (32, 32), f"size is 32x32 (got {w}x{h})")
    opaque = [(x, y) for y in range(h) for x in range(w) if px[y][x][3] == 255]
    soft = [(x, y) for y in range(h) for x in range(w) if 0 < px[y][x][3] < 255]
    check(len(opaque) > 0, f"{len(opaque)} opaque pixels, {len(soft)} half-transparent")
    if opaque:
        xs = [p[0] for p in opaque + soft]
        ys = [p[1] for p in opaque + soft]
        print(f"     visible shape: {max(xs) - min(xs) + 1} x {max(ys) - min(ys) + 1} px "
              f"at ({min(xs)},{min(ys)})")
        check(15 <= max(ys) - min(ys) + 1 <= 26, "visible height is about 20-24 px")

    colours = {}
    for y in range(h):
        for x in range(w):
            if px[y][x][3]:
                colours[px[y][x][:3]] = colours.get(px[y][x][:3], 0) + 1
    greys = sum(n for (r, g, b), n in colours.items() if r == g == b and 0 < r < 255)
    others = {c: n for c, n in colours.items() if not (c[0] == c[1] == c[2])}
    print(f"     colours: black x{colours.get((0, 0, 0), 0)}, white x{colours.get((255, 255, 255), 0)}, "
          f"grey smoothing x{greys}" + (f", other x{sum(others.values())}" if others else ""))
    check(not others, "only black, white and greys between them (plus transparency)"
          + (f"; other colours: {['#%02x%02x%02x' % c for c in list(others)[:5]]}" if others else ""))

    # White edge: no black pixel may touch transparency (4-neighbourhood).
    def at(x, y):
        return px[y][x] if 0 <= x < w and 0 <= y < h else (0, 0, 0, 0)
    leaks = [(x, y) for y in range(h) for x in range(w)
             if px[y][x][3] == 255 and px[y][x][:3] == (0, 0, 0)
             and any(at(x + dx, y + dy)[3] == 0 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    check(not leaks, "white edge is continuous (no black pixel touches transparency)"
          + (f"; gaps at {leaks[:6]}" for _ in [0]).__next__() if leaks else
          "white edge is continuous (no black pixel touches transparency)")

    if opaque:
        tip = min(opaque, key=lambda p: (p[0] + p[1], p[1]))
        print(f"     likely hotspot (top-left-most opaque pixel): {tip[0]},{tip[1]}")

    if len(sys.argv) > 2:
        pad = 8
        cell = w * ZOOM + pad
        W, H = len(BACKGROUNDS) * cell + pad, pad + h + pad + h * ZOOM + pad
        out = [[(40, 40, 40, 255)] * W for _ in range(H)]
        for i, bg in enumerate(BACKGROUNDS):
            x0 = pad + i * cell
            for y in range(pad, H - pad):
                for x in range(x0, x0 + w * ZOOM):
                    out[y][x] = bg + (255,)
            for y in range(h):
                for x in range(w):
                    c = blend(px[y][x], bg)
                    out[pad + y][x0 + x] = c                          # 1x
                    for yy in range(ZOOM):
                        for xx in range(ZOOM):
                            out[pad + h + pad + y * ZOOM + yy][x0 + x * ZOOM + xx] = c
        write_rgba(sys.argv[2], W, H, out)
        print(f"     preview: {sys.argv[2]} (1x on top, 8x below; navy, white, sky)")

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

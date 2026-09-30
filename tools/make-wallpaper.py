#!/usr/bin/env python3
"""LiteKern X — draw the "crossing" wallpaper (docs/ASSET-PROMPTS.md §8).

    python3 tools/make-wallpaper.py            # assets/wallpapers/crossing.png
    python3 tools/make-wallpaper.py --light    # assets/wallpapers/crossing-light.png
    python3 tools/make-wallpaper.py --logo     # assets/logo.png (128x128, from the §6 spec)

Everything is drawn from maths here (no source images), so the result is ours
to ship. The picture: dark slate, two soft waves rising at the bottom, a large
glowing "X" made of two light ribbons in the lower right. The upper-middle,
behind the app grid, is kept dark and plain. The logo itself is not in the
picture; --logo renders it on its own (it matches assets/logo.svg).

Needs numpy and Pillow. Prints the checks from §8.3 when done.
"""
import argparse
import io
import os
import sys

import numpy as np
from PIL import Image

W, H = 1024, 600
GRID = (150, 30, 870, 400)          # x0, y0, x1, y1: the app grid area (§8.1)


def rgb(hex_):
    hex_ = hex_.lstrip("#")
    return np.array([int(hex_[i:i + 2], 16) / 255 for i in (0, 2, 4)])


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def over(dst, colour, alpha):
    """Paint `colour` (3,) or (H,W,3) over dst with per-pixel alpha (H,W)."""
    a = alpha[..., None]
    return dst * (1 - a) + colour * a


def screen(dst, colour, amount):
    """Screen-blend a glow: brightens without ever clipping to flat white."""
    c = np.clip(colour * amount[..., None], 0, 1)
    return 1 - (1 - dst) * (1 - c)


def ribbon(yy, xx, cx, cy, angle_deg, half_width, length):
    """A soft band through (cx, cy): 1 on its centre line, fading across its
    width and towards its ends. Returns (strength, distance across)."""
    a = np.radians(angle_deg)
    dx, dy = np.cos(a), np.sin(a)
    rx, ry = xx - cx, yy - cy
    along = rx * dx + ry * dy
    across = -rx * dy + ry * dx
    body = np.exp(-(across / half_width) ** 2)
    ends = np.exp(-(along / length) ** 4)     # flat middle, soft tips
    return body * ends, across


def palette(light):
    if light:
        return dict(
            top=rgb("#f6f5f4"), bottom=rgb("#dfe6f2"),
            wave1=rgb("#99c1f1"), wave2=rgb("#c3b4e0"),
            glow_a=rgb("#62a0ea"), glow_b=rgb("#c061cb"),
            x_a=rgb("#3584e4"), x_b=rgb("#9141ac"),
            gap=rgb("#e4e9f2"),
        )
    return dict(
        top=rgb("#202634"), bottom=rgb("#1b2030"),
        wave1=rgb("#1a5fb4"), wave2=rgb("#613583"),
        glow_a=rgb("#1c71d8"), glow_b=rgb("#9141ac"),
        x_a=rgb("#48A6E8"), x_b=rgb("#C8D0DC"),
        gap=rgb("#1b2030"),
    )


def draw_background(p, light):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    ny = yy / (H - 1)

    img = p["top"] * (1 - ny[..., None]) + p["bottom"] * ny[..., None]

    # Soft coloured light low in the picture, left (purple) and right (blue).
    glow_l = np.exp(-(((xx - 120) / 420) ** 2 + ((yy - 640) / 230) ** 2))
    glow_r = np.exp(-(((xx - 900) / 380) ** 2 + ((yy - 620) / 260) ** 2))
    k = 0.30 if light else 0.55
    img = screen(img, p["glow_b"], glow_l * k)
    img = screen(img, p["glow_a"], glow_r * k)

    # Two large waves rising from the bottom (Fedora style), each with a soft
    # 3 px edge and a gentle vertical gradient inside.
    for base, amp, freq, phase, colour, alpha in (
        (505, 28, 1.3, 0.6, p["wave2"], 0.55 if light else 0.70),
        (548, 22, 1.0, 2.4, p["wave1"], 0.55 if light else 0.75),
    ):
        edge = base + amp * np.sin(xx / W * 2 * np.pi * freq + phase)
        inside = smoothstep(-1.5, 1.5, yy - edge)
        depth = np.clip((yy - edge) / 90, 0, 1)
        shade = colour * (1.0 - 0.25 * depth[..., None])
        # a faint bright rim along the crest
        rim = np.exp(-((yy - edge) / 2.2) ** 2) * 0.25
        img = over(img, shade, inside * alpha)
        img = screen(img, colour * 1.6, rim)

    # The big "X": two soft ribbons crossing low on the right, fading out
    # before they reach the app grid or the screen edges. Like the logo, the
    # sky ribbon lies on top of the mist one, with a thin dark gap.
    cx, cy = 925, 472
    rb, _ = ribbon(yy, xx, cx, cy, -45, 20, 112)        # mist, underneath
    ra, across_a = ribbon(yy, xx, cx, cy, 45, 20, 112)  # sky, on top
    near = np.exp(-(((xx - cx) ** 2 + (yy - cy) ** 2) / 50 ** 2))
    gap = np.exp(-((np.abs(across_a) - 25) / 4) ** 2) * near
    halo = np.exp(-(((xx - cx) ** 2 + (yy - cy) ** 2) / 140 ** 2))
    # the arm pointing up-left fades out before it reaches the app grid
    gx0, gy0, gx1, gy1 = GRID
    in_grid = smoothstep(gx1 + 40, gx1 - 60, xx) * smoothstep(gy1 + 40, gy1 - 60, yy)
    keep = 1 - 0.8 * in_grid
    ra, rb, gap, halo = ra * keep, rb * keep, gap * keep, halo * keep
    if light:
        # on a light background a glow can't brighten, so paint the ribbons
        img = over(img, p["x_b"], rb * 0.45)
        img = over(img, p["gap"], gap * 0.7)
        img = over(img, p["x_a"], ra * 0.55)
        img = over(img, p["glow_a"], halo * 0.06)
    else:
        img = screen(img, p["x_b"] * 0.8, rb * 0.8)
        img = over(img, p["gap"], gap * 0.55)
        img = screen(img, p["x_a"], ra * 0.9)
        img = screen(img, p["x_a"], halo * 0.18)

    # Keep the app grid calm: gently pull everything in the upper-middle back
    # towards the base colour, with a soft edge so there's no visible box.
    gx0, gy0, gx1, gy1 = GRID
    mx = smoothstep(gx0 - 90, gx0, xx) * (1 - smoothstep(gx1, gx1 + 90, xx))
    my = 1 - smoothstep(gy1, gy1 + 90, yy)
    calm = mx * my * (0.55 if light else 0.6)
    img = over(img, p["top"], calm)

    # Calm edges: other screens fill the space round the wallpaper with its
    # edge colour, so ease the last 24 px towards one even tone.
    edge = 1 - smoothstep(0, 24, np.minimum(yy, H - 1 - yy))
    edge_top = img[0].mean(axis=0)
    edge_bottom = img[-1].mean(axis=0)
    target = np.where((yy < H / 2)[..., None], edge_top, edge_bottom)
    img = over(img, target, edge * 0.85)

    return np.clip(img, 0, 1)


def draw_logo(size=96, ss=4):
    """The §6 logo at `size` px, drawn 4x larger and averaged down so edges
    are smooth. Returns RGBA float (size, size, 4)."""
    n = size * ss
    s = n / 128                                     # the SVG is 128 units
    yy, xx = (np.mgrid[0:n, 0:n] + 0.5) / s         # in SVG units

    def rounded_rect(x0, y0, x1, y1, r):
        qx = np.maximum(np.maximum(x0 + r - xx, xx - (x1 - r)), 0)
        qy = np.maximum(np.maximum(y0 + r - yy, yy - (y1 - r)), 0)
        return np.hypot(qx, qy) <= r

    def bar(angle, half_len, half_w):
        a = np.radians(angle)
        dx, dy = np.cos(a), np.sin(a)
        rx, ry = xx - 64, yy - 64
        along = np.abs(rx * dx + ry * dy)
        across = np.abs(-rx * dy + ry * dx)
        # rounded ends
        q = np.maximum(along - (half_len - half_w), 0)
        return np.hypot(q, across) <= half_w

    ink, navy = rgb("#0F1E33"), rgb("#1E3A5F")
    sky, mist = rgb("#48A6E8"), rgb("#C8D0DC")

    out = np.zeros((n, n, 4))
    outer = rounded_rect(1, 1, 127, 127, 24)
    inner = rounded_rect(3, 3, 125, 125, 22)
    out[outer] = [*ink, 1]
    out[inner] = [*navy, 1]
    mist_bar = bar(-45, 40, 11) & inner
    sky_gap = bar(45, 40, 13) & inner
    sky_bar = bar(45, 40, 11) & inner
    out[mist_bar] = [*mist, 1]
    out[sky_gap] = [*navy, 1]
    out[sky_bar] = [*sky, 1]

    # average each ss x ss block (premultiplied, so edges blend correctly)
    pre = out.copy()
    pre[..., :3] *= pre[..., 3:4]
    pre = pre.reshape(size, ss, size, ss, 4).mean(axis=(1, 3))
    a = pre[..., 3:4]
    pre[..., :3] = np.where(a > 0, pre[..., :3] / np.maximum(a, 1e-9), 0)
    return pre


def dither_to_8bit(img):
    """Quantise with an 8x8 ordered dither, so slow gradients don't break
    into stripes. Ordered (not random) dither keeps the PNG small."""
    b = np.array([[0, 32, 8, 40, 2, 34, 10, 42],
                  [48, 16, 56, 24, 50, 18, 58, 26],
                  [12, 44, 4, 36, 14, 46, 6, 38],
                  [60, 28, 52, 20, 62, 30, 54, 22],
                  [3, 35, 11, 43, 1, 33, 9, 41],
                  [51, 19, 59, 27, 49, 17, 57, 25],
                  [15, 47, 7, 39, 13, 45, 5, 37],
                  [63, 31, 55, 23, 61, 29, 53, 21]]) / 64.0
    t = np.tile(b, (H // 8 + 1, W // 8 + 1))[:H, :W, None]
    return np.clip(np.floor(img * 255 + t), 0, 255).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--light", action="store_true")
    ap.add_argument("--logo", action="store_true")
    ap.add_argument("--out")
    args = ap.parse_args()

    if args.logo:
        logo = np.clip(np.round(draw_logo(128) * 255), 0, 255).astype(np.uint8)
        out = args.out or os.path.join(os.path.dirname(__file__), "..", "assets", "logo.png")
        Image.fromarray(logo, "RGBA").save(out, "PNG", optimize=True)
        print(f"{os.path.normpath(out)}: 128x128 RGBA, {os.path.getsize(out) / 1024:.1f} KB")
        return

    p = palette(args.light)
    img = draw_background(p, args.light)

    px = dither_to_8bit(img)

    name = "crossing-light.png" if args.light else "crossing.png"
    out = args.out or os.path.join(os.path.dirname(__file__), "..", "assets", "wallpapers", name)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    buf = io.BytesIO()
    Image.fromarray(px, "RGB").save(buf, "PNG", optimize=True, compress_level=9)
    with open(out, "wb") as f:
        f.write(buf.getvalue())

    # §8.3 checks
    size_kb = len(buf.getvalue()) / 1024
    gx0, gy0, gx1, gy1 = GRID
    grid = px[gy0:gy1, gx0:gx1].astype(np.float64) / 255
    luma = grid @ np.array([0.2126, 0.7152, 0.0722])
    print(f"{os.path.normpath(out)}: {W}x{H} RGB, {size_kb:.0f} KB "
          f"({'ok' if size_kb <= 300 else 'over goal' if size_kb <= 512 else 'OVER LIMIT'})")
    print(f"app grid brightness: mean {luma.mean():.1%}, max {luma.max():.1%}")
    over25 = (luma > 0.25).mean()
    if not args.light:
        print(f"app grid pixels over 25%: {over25:.2%}")
        if over25 > 0.001:
            print("warning: part of the app grid area is brighter than 25%", file=sys.stderr)
    spread = max(np.ptp(px[0].astype(int), axis=0).max(), np.ptp(px[-1].astype(int), axis=0).max())
    print(f"edge rows vary by at most {spread} levels (calm if small)")


if __name__ == "__main__":
    main()

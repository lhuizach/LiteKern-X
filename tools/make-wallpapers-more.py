#!/usr/bin/env python3
"""LiteKern X - draw three more wallpapers (Phase 3 §5: "3-5 images").

    python tools/make-wallpapers-more.py      # assets/wallpapers/{dusk,aurora,drift}[-light].png

Companions to "crossing" (tools/make-wallpaper.py), drawn from maths the same
way (no source images, so they're ours to ship) and following
docs/ASSET-PROMPTS.md §8: 1024x600, the upper-middle (the app grid) dark and
calm in the dark versions, light and calm in the light ones, colour and
detail low in the picture. Reuses make-wallpaper.py's helpers and its
ordered dither (no banding, small PNGs). Needs numpy and Pillow.

  dusk    layered hills rising from the bottom under a dusk glow
  aurora  a soft green-teal aurora curtain over a dark horizon
  drift   large soft overlapping shapes drifting in from the lower right
"""
import importlib.util
import io
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("mw", os.path.join(HERE, "make-wallpaper.py"))
mw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mw)
W, H, GRID = mw.W, mw.H, mw.GRID
rgb, smoothstep, over, screen = mw.rgb, mw.smoothstep, mw.over, mw.screen


def grid_calm(img, base, xx, yy, amount):
    """Pull the app grid area back towards the base colour, soft-edged."""
    gx0, gy0, gx1, gy1 = GRID
    soft = 320                      # a wide fade: no visible box
    mx = smoothstep(gx0 - soft, gx0 + 60, xx) * (1 - smoothstep(gx1 - 60, gx1 + soft, xx))
    my = 1 - smoothstep(gy1 - 140, gy1 + soft, yy)
    return over(img, base, mx * my * amount)


def dusk(light):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    ny = yy / (H - 1)
    if light:
        top, horizon = rgb("#f6f5f4"), rgb("#f2d9e6")
        hills = [rgb("#c3b4e0"), rgb("#a8b6e8"), rgb("#8fa3d9")]
        glow = rgb("#f6c3a8")
    else:
        top, horizon = rgb("#202634"), rgb("#3d2a52")
        hills = [rgb("#3b2766"), rgb("#2a3a7a"), rgb("#1d2b5e")]
        glow = rgb("#c061cb")
    img = top * (1 - ny[..., None] ** 1.6) + horizon * (ny[..., None] ** 1.6)
    sun = np.exp(-(((xx - 300) / 520) ** 2 + ((yy - 560) / 170) ** 2))
    img = over(img, glow, sun * (0.35 if light else 0.25))
    # three hill lines, each a sum of slow sines, farthest first
    for i, (base, colour) in enumerate(zip((470, 515, 560), hills)):
        edge = (base + 26 * np.sin(xx / W * 2 * np.pi * (0.8 + 0.35 * i) + 1.3 * i)
                + 12 * np.sin(xx / W * 2 * np.pi * (2.1 + 0.5 * i) + 0.7 + i))
        inside = smoothstep(-1.5, 1.5, yy - edge)
        shade = colour * (1.0 - 0.18 * np.clip((yy - edge) / 120, 0, 1))[..., None]
        img = over(img, shade, inside * (0.85 if light else 0.92))
        rim = np.exp(-((yy - edge) / 2.0) ** 2) * (0.10 if light else 0.18)
        img = screen(img, colour * 1.5, rim) if not light else over(img, rgb("#ffffff"), rim)
    return grid_calm(img, top, xx, yy, 0.35 if light else 0.4), top


def aurora(light):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    ny = yy / (H - 1)
    if light:
        top, bottom = rgb("#f6f5f4"), rgb("#dceee8")
        curtain_a, curtain_b, land = rgb("#57e389"), rgb("#62a0ea"), rgb("#9fb3c8")
    else:
        top, bottom = rgb("#1b2030"), rgb("#14222a")
        curtain_a, curtain_b, land = rgb("#2ec27e"), rgb("#1c71d8"), rgb("#0e141c")
    img = top * (1 - ny[..., None]) + bottom * ny[..., None]
    # the curtain: a wavy band low in the sky, brighter at its lower edge,
    # with faint vertical rays
    centre = 500 + 26 * np.sin(xx / W * 2 * np.pi * 1.2 + 0.4) + 18 * np.sin(xx / W * 2 * np.pi * 3.1)
    d = yy - centre
    band = np.exp(-(d / 55) ** 2) * (1 - smoothstep(10, 40, d))
    rays = 0.75 + 0.25 * np.sin(xx / 7.0 + 2 * np.sin(xx / 61.0))
    fade = smoothstep(80, 360, xx) * (1 - smoothstep(940, 1024, xx))
    a = band * rays * fade
    mix = np.clip((xx - 200) / 700, 0, 1)[..., None]
    colour = curtain_a * (1 - mix) + curtain_b * mix
    if light:
        img = over(img, colour, a * 0.35)
    else:
        img = screen(img, colour, a * 0.55)
    # a low dark land line
    edge = 548 + 10 * np.sin(xx / W * 2 * np.pi * 1.7 + 1.1) + 6 * np.sin(xx / W * 2 * np.pi * 4.3)
    img = over(img, land, smoothstep(-1.5, 1.5, yy - edge) * 0.95)
    return grid_calm(img, top, xx, yy, 0.3 if light else 0.35), top


def drift(light):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
    ny = yy / (H - 1)
    if light:
        top, bottom = rgb("#f6f5f4"), rgb("#e6e9f2")
        shapes = [rgb("#99c1f1"), rgb("#dc8add"), rgb("#93ddc2"), rgb("#62a0ea")]
    else:
        top, bottom = rgb("#202634"), rgb("#1a1f2c")
        shapes = [rgb("#1a5fb4"), rgb("#813d9c"), rgb("#26a269"), rgb("#1c71d8")]
    img = top * (1 - ny[..., None]) + bottom * ny[..., None]
    # soft discs, biggest first, gathered in the lower right
    for (cx, cy, r, colour, a) in ((880, 560, 300, shapes[0], 0.55), (640, 640, 230, shapes[1], 0.45),
                                   (1070, 430, 160, shapes[2], 0.40), (830, 540, 120, shapes[3], 0.50)):
        dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        disc = 1 - smoothstep(r - 40, r + 40, dist)
        if light:
            img = over(img, colour, disc * a * 0.6)
        else:
            img = screen(img, colour, disc * a)
    return grid_calm(img, top, xx, yy, 0.35 if light else 0.45), top


def save(img, name):
    px = mw.dither_to_8bit(np.clip(img, 0, 1))
    out = os.path.join(HERE, "..", "assets", "wallpapers", name + ".png")
    buf = io.BytesIO()
    Image.fromarray(px, "RGB").save(buf, "PNG", optimize=True, compress_level=9)
    open(out, "wb").write(buf.getvalue())
    gx0, gy0, gx1, gy1 = GRID
    luma = (px[gy0:gy1, gx0:gx1].astype(np.float64) / 255) @ np.array([0.2126, 0.7152, 0.0722])
    print(f"{name}.png: {len(buf.getvalue()) / 1024:.0f} KB, app grid brightness mean "
          f"{luma.mean():.1%} max {luma.max():.1%}")


def main():
    for draw, name in ((dusk, "dusk"), (aurora, "aurora"), (drift, "drift")):
        for light in (False, True):
            img, _ = draw(light)
            save(img, name + ("-light" if light else ""))


if __name__ == "__main__":
    sys.exit(main())

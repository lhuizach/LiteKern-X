#!/usr/bin/env python3
"""LiteKern X — draw the Settings app icon (Adwaita style) at 1024x1024.

    python3 tools/make-icon-settings.py     # assets/icons/source/settings-1024.png

A gear seen from the front with eight broad, rounded teeth: grey #9a9996,
blending to light grey #c0bfbc over its top half, and a large round hole in
the middle filled with dark grey #5e5c64. The teeth are wide and the hole big
so it reads at 48x48. The shared style (shapes, top light, preview) is in
tools/adwaita_icon.py.
"""
import argparse

import numpy as np

from adwaita_icon import (BODY, N, Icon, circle, polygon, rgb, smooth_union,
                          source_path, yy)

GREY, LIGHT_GREY, DARK_GREY = rgb("#9a9996"), rgb("#c0bfbc"), rgb("#5e5c64")

C = N / 2
R_TIP = BODY / 2         # 425: tooth tips touch the 83% box
R_ROOT = 330             # the body circle the teeth stand on
R_HOLE = 150
TEETH = 8
ROUND = 36               # corner radius of each tooth


def tooth(angle):
    """A slightly tapered tooth pointing out at `angle`, with rounded corners.
    Drawn as a polygon inset by ROUND, then grown back by ROUND."""
    inner_r, outer_r = R_ROOT - 40, R_TIP - ROUND
    inner_hw, outer_hw = 92 - ROUND, 70 - ROUND        # half-widths before rounding
    d = np.array([np.cos(angle), np.sin(angle)])       # outwards
    n = np.array([-d[1], d[0]])                        # across
    pts = [C + d * inner_r - n * inner_hw, C + d * outer_r - n * outer_hw,
           C + d * outer_r + n * outer_hw, C + d * inner_r + n * inner_hw]
    # polygon() wants clockwise on screen; flip if this order isn't
    a, b, c = pts[0], pts[1], pts[2]
    if (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) < 0:
        pts = pts[::-1]
    return polygon([tuple(p) for p in pts]) - ROUND


def draw():
    icon = Icon()

    gear = circle(C, C, R_ROOT)
    for k in range(TEETH):
        gear = smooth_union(gear, tooth(k * 2 * np.pi / TEETH), 28)   # soft roots

    # light grey at the top, blending to grey through the middle
    t = np.clip((yy - (C - 160)) / 320, 0, 1)[..., None]
    t = t * t * (3 - 2 * t)
    body_colour = LIGHT_GREY * (1 - t) + GREY * t
    icon.paint(gear, body_colour, C - R_TIP, C + R_TIP)

    hole = circle(C, C, R_HOLE)
    icon.paint(hole, DARK_GREY, C - R_HOLE, C + R_HOLE)
    return icon


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    draw().save(args.out or source_path("settings"))


if __name__ == "__main__":
    main()

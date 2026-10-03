#!/usr/bin/env python3
"""LiteKern X — draw the Log app icon (Adwaita style) at 1024x1024.

    python3 tools/make-icon-log.py     # assets/icons/source/log-1024.png

A terminal-like window, a little wider than tall: a dark grey #5e5c64 frame
with a thicker title-bar band across the top, a near-black #241f31 screen,
and in the screen's upper left a bold white ">" prompt with a thick white "_"
cursor beside it. Strokes are thick so they read at 48x48. The shared style
(shapes, top light, preview) is in tools/adwaita_icon.py.
"""
import argparse

import numpy as np

from adwaita_icon import (MARGIN, N, Icon, rgb, rounded_rect, source_path,
                          xx, yy)

FRAME, SCREEN, WHITE = rgb("#5e5c64"), rgb("#241f31"), rgb("#ffffff")

X0, X1 = MARGIN, N - MARGIN              # 850 wide
Y0, Y1 = (N - 700) / 2, (N + 700) / 2    # 700 tall
TITLE = 100                              # title-bar band
BORDER = 36                              # frame on the other three sides
STROKE = 60                              # thickness of ">" and "_"


def capsule(ax, ay, bx, by, r):
    """A line from a to b with round ends, r thick on each side."""
    ex, ey = bx - ax, by - ay
    wx, wy = xx - ax, yy - ay
    t = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1)
    return np.hypot(wx - ex * t, wy - ey * t) - r


def draw():
    icon = Icon()

    window = rounded_rect(X0, Y0, X1, Y1, 80)
    icon.paint(window, FRAME, Y0, Y1)

    sx0, sy0, sx1, sy1 = X0 + BORDER, Y0 + TITLE, X1 - BORDER, Y1 - BORDER
    screen = rounded_rect(sx0, sy0, sx1, sy1, 48)
    icon.paint(screen, SCREEN, sy0, sy1)

    # ">" : two thick strokes meeting at a rounded point
    left, top, tip_x, h = sx0 + 90, sy0 + 80, sx0 + 210, 220
    tip_y = top + h / 2
    prompt = np.minimum(capsule(left, top, tip_x, tip_y, STROKE / 2),
                        capsule(tip_x, tip_y, left, top + h, STROKE / 2))
    icon.paint(prompt, WHITE, top - STROKE / 2, top + h + STROKE / 2)

    # "_" : a short, thick bar on the prompt's baseline
    ux0 = tip_x + 70
    cursor = rounded_rect(ux0, top + h - STROKE / 2, ux0 + 150, top + h + STROKE / 2, 18)
    icon.paint(cursor, WHITE, top + h - STROKE / 2, top + h + STROKE / 2)
    return icon


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    draw().save(args.out or source_path("log"))


if __name__ == "__main__":
    main()

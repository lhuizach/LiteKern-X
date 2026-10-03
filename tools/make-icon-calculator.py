#!/usr/bin/env python3
"""LiteKern X — draw the Calculator app icon (Adwaita style) at 1024x1024.

    python3 tools/make-icon-calculator.py     # assets/icons/source/calculator-1024.png

An upright calculator: a rounded dark grey #3d3846 body, a blank light grey
#deddda display strip across the top, and a 3x3 grid of big round light grey
keys below it, the bottom-right one orange #ff7800. The keys are large and
evenly spaced so the grid still shows at 48x48. The shared style (shapes, top
light, preview) is in tools/adwaita_icon.py.
"""
import argparse

from adwaita_icon import (MARGIN, N, Icon, circle, rgb, rounded_rect,
                          source_path)

DARK, LIGHT, ORANGE = rgb("#3d3846"), rgb("#deddda"), rgb("#ff7800")

KEY = 140        # key diameter
GAP = 50         # between keys, and between the display and the keys
SIDE = 70        # body edge to keys/display, left and right
END = 60         # body edge to display (top) and keys (bottom)

W = 2 * SIDE + 3 * KEY + 2 * GAP          # 660: a little narrower than tall
X0, X1 = (N - W) / 2, (N + W) / 2
Y0, Y1 = MARGIN, N - MARGIN               # 850 tall
DISPLAY_H = (Y1 - Y0) - 2 * END - 3 * KEY - 3 * GAP   # what's left: 160


def draw():
    icon = Icon()

    body = rounded_rect(X0, Y0, X1, Y1, 96)
    icon.paint(body, DARK, Y0, Y1)

    dx0, dx1 = X0 + SIDE, X1 - SIDE
    dy0 = Y0 + END
    dy1 = dy0 + DISPLAY_H
    display = rounded_rect(dx0, dy0, dx1, dy1, 36)
    icon.paint(display, LIGHT, dy0, dy1)

    for row in range(3):
        for col in range(3):
            cx = dx0 + KEY / 2 + col * (KEY + GAP)
            cy = dy1 + GAP + KEY / 2 + row * (KEY + GAP)
            colour = ORANGE if (row, col) == (2, 2) else LIGHT
            icon.paint(circle(cx, cy, KEY / 2), colour, cy - KEY / 2, cy + KEY / 2)
    return icon


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    draw().save(args.out or source_path("calculator"))


if __name__ == "__main__":
    main()

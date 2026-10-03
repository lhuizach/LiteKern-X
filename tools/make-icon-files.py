#!/usr/bin/env python3
"""LiteKern X — draw the Files app icon (Adwaita style) at 1024x1024.

    python3 tools/make-icon-files.py     # assets/icons/source/files-1024.png

A folder seen from the front: the back and its tab in blue #3584e4, a light
grey #deddda sheet peeking out, and the front flap in light blue #62a0ea
covering the lower two thirds. The shared style (shapes, top light, preview)
is in tools/adwaita_icon.py.
"""
import argparse

import numpy as np

from adwaita_icon import (MARGIN, N, Icon, polygon, rgb, rounded_rect,
                          smooth_union, source_path, xx)

X0, X1 = MARGIN, N - MARGIN              # 850 wide
Y0, Y1 = (N - 700) / 2, (N + 700) / 2    # the folder is wider than tall: 850x700

BLUE, LIGHT_BLUE, GREY = rgb("#3584e4"), rgb("#62a0ea"), rgb("#deddda")


def draw():
    r = 56
    icon = Icon()

    # back + tab (one shape, with a soft fillet where the tab meets the back)
    back_top = Y0 + 72
    back = rounded_rect(X0, back_top, X1, Y1, r)
    # the tab: rounded, with a sloped right side; it runs down past the back's
    # top-left corner so the folder's left edge is one straight line
    tab = polygon([(X0 + 40, Y0 + 40), (X0 + 268, Y0 + 40), (X0 + 330, back_top + 60),
                   (X0 + 40, back_top + 60)]) - 40
    # a soft fillet where the slope meets the back, but a hard join along the
    # shared left edge (a smooth union there would bulge outwards)
    soft = smooth_union(back, tab, 40)
    back = np.where(xx > X0 + 150, soft, np.minimum(back, tab))
    icon.paint(back, BLUE, Y0, Y1)

    # the sheet of paper, peeking out between back and front
    front_top = Y1 - (Y1 - Y0) * 2 / 3
    paper = rounded_rect(X0 + 80, back_top + 44, X1 - 80, front_top + 120, 28)
    icon.paint(paper, GREY, back_top + 44, front_top + 120)

    # the front flap: the lower two thirds
    front = rounded_rect(X0, front_top, X1, Y1, r)
    icon.paint(front, LIGHT_BLUE, front_top, Y1)
    return icon


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    args = ap.parse_args()
    draw().save(args.out or source_path("files"))


if __name__ == "__main__":
    main()

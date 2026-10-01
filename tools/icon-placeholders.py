#!/usr/bin/env python3
"""LiteKern X - draw the placeholder app icons (48x48 RGBA PNG, Adwaita style).

Usage: python3 tools/icon-placeholders.py [OUT_DIR]   (default assets/icons)

Stand-ins until real icons are made (docs/ASSET-PROMPTS.md §4): a blue
folder for Files, a terminal for Log, a gear for Settings and a calculator. Shapes are sampled 4x4 per pixel
for smooth edges. Only needs the standard library.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lkx_png import write_rgba  # noqa: E402

SIZE, SS = 48, 4


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def round_rect(x0, y0, x1, y1, r):
    def inside(x, y):
        if not (x0 <= x < x1 and y0 <= y < y1):
            return False
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return inside


def thick_line(ax, ay, bx, by, half):
    def inside(x, y):
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
        px, py = ax + t * dx, ay + t * dy
        return (x - px) ** 2 + (y - py) ** 2 <= half * half
    return inside


def render(layers):
    """layers: [(inside(x, y), 0xRRGGBB)], painted in order."""
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            acc = [0.0, 0.0, 0.0, 0.0]          # premultiplied r, g, b, a
            for sy in range(SS):
                for sx in range(SS):
                    fx, fy = x + (sx + 0.5) / SS, y + (sy + 0.5) / SS
                    c, a = (0, 0, 0), 0.0
                    for inside, colour in layers:
                        if inside(fx, fy):
                            c, a = rgb(colour), 1.0
                    for i in range(3):
                        acc[i] += c[i] * a
                    acc[3] += a
            a = acc[3] / (SS * SS)
            if a == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append(tuple(round(acc[i] / acc[3]) for i in range(3)) + (round(a * 255),))
        rows.append(row)
    return rows


def folder():
    return render([
        (round_rect(4, 7, 22, 14, 3), 0x3584e4),     # tab
        (round_rect(4, 10, 44, 40, 4), 0x3584e4),    # back
        (round_rect(8, 13, 40, 26, 1), 0xdeddda),    # paper
        (round_rect(4, 17, 44, 42, 4), 0x62a0ea),    # front
        (round_rect(4, 38, 44, 42, 4), 0x438de6),    # front's lower edge
    ])


def terminal():
    return render([
        (round_rect(5, 8, 43, 41, 5), 0x5e5c64),     # frame
        (round_rect(7, 14, 41, 39, 3), 0x241f31),    # screen
        (thick_line(12, 20, 18, 25, 1.4), 0xffffff),  # >
        (thick_line(18, 25, 12, 30, 1.4), 0xffffff),
        (round_rect(21, 30, 30, 32.6, 1), 0xffffff),  # _
    ])


def circle(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def gear_shape(cx, cy, r_in, r_out, teeth):
    import math

    def inside(x, y):
        dx, dy = x - cx, y - cy
        d = math.hypot(dx, dy)
        if d <= r_in:
            return True
        if d > r_out:
            return False
        a = (math.atan2(dy, dx) / (2 * math.pi) * teeth) % 1.0
        return 0.25 <= a <= 0.75            # a tooth half the pitch wide
    return inside


def gear():
    return render([
        (gear_shape(24, 24, 15.5, 20.5, 8), 0x77767b),  # body and teeth
        (circle(24, 24, 13), 0x9a9996),                 # face
        (circle(24, 24, 6.5), 0x5e5c64),                # hub
        (circle(24, 24, 3.5), 0x3d3846),                # hole
    ])


def calculator():
    layers = [
        (round_rect(8, 4, 40, 45, 5), 0x9a9996),        # body (shadow edge)
        (round_rect(8, 4, 40, 43, 5), 0xdeddda),        # body
        (round_rect(12, 8, 36, 17, 2), 0x241f31),       # display
    ]
    for row in range(3):
        for col in range(3):
            x0, y0 = 12 + col * 8.5, 21 + row * 7.5
            colour = 0xff7800 if (row, col) == (2, 2) else 0x77767b
            layers.append((round_rect(x0, y0, x0 + 6.5, y0 + 5.5, 1.2), colour))
    return render(layers)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/icons"
    os.makedirs(out, exist_ok=True)
    for name, rows in (("files", folder()), ("log", terminal()), ("settings", gear()),
                       ("calculator", calculator())):
        write_rgba(os.path.join(out, name + ".png"), SIZE, SIZE, rows)
        print(f"icon-placeholders: {out}/{name}.png")


if __name__ == "__main__":
    main()

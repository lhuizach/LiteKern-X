#!/usr/bin/env python3
"""LiteKern X - rasterise the GUI font into an atlas (Phase 3 section 5).

Usage: python tools/font-rasterize.py FONT.ttf assets/fonts

Needs Pillow (FreeType), so it's run by hand, not by the build: the output
(ui-font.png, a grayscale atlas, and ui-font.json, its metrics) is
committed, and tools/font2c.py turns it into C at build time with only the
standard library. Rerun it only to change the font or the sizes below.

The faces are the text styles the GUI uses (kernel/text.h). Glyphs are
printable ASCII, hinted by FreeType for crispness at 1:1 on the EeePC's panel,
with fractional advances kept (1/64 px) so spacing stays even.
"""
import json
import os
import sys

from PIL import Image, ImageFont

FACES = [                       # name, pixel size, weight (variable font's wght axis)
    ("body", 15, 400),
    ("bold", 15, 700),
    ("small", 13, 400),
    ("heading", 20, 700),
    ("large", 32, 300),
]
CHARS = [chr(c) for c in range(32, 127)]
# Beyond ASCII, after '~': these Unicode characters, reached in C as the bytes
# 0x80, 0x81, ... (kernel/text.h's TEXT_MINUS etc.).
EXTRA = ["−", "×", "÷", "…"]     # minus, times, divide, ellipsis
CHARS += EXTRA


def face(path, size, weight):
    f = ImageFont.truetype(path, size)
    try:
        axes = f.get_variation_axes()
        values = []
        for a in axes:
            name = a.get("name", b"")
            name = name.decode() if isinstance(name, bytes) else name
            values.append(weight if name.lower().startswith("weight") else a.get("default", 100))
        f.set_variation_by_axes(values)
    except Exception:
        pass                    # not a variable font: use it as it is
    return f


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: font-rasterize.py FONT.ttf OUT_DIR")
    path, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    meta = {"version": 1, "source": os.path.basename(path), "extra": EXTRA, "faces": []}
    rows = []                   # (face index, char, mask)
    for name, size, weight in FACES:
        f = face(path, size, weight)
        ascent, descent = f.getmetrics()
        info = {"name": name, "size": size, "weight": weight, "ascent": ascent,
                "descent": descent, "glyphs": []}
        for ch in CHARS:
            mask, (xoff, yoff) = f.getmask2(ch, mode="L")
            w, h = mask.size
            img = Image.frombytes("L", (w, h), bytes(mask)) if w and h else None
            info["glyphs"].append({"c": ord(ch), "w": w, "h": h, "xoff": xoff, "yoff": yoff,
                                   "adv64": round(f.getlength(ch) * 64)})
            rows.append((len(meta["faces"]), ch, img))
        meta["faces"].append(info)

    # Pack glyphs in rows, 512 px wide.
    W, x, y, row_h = 512, 0, 0, 0
    place = []
    for fi, ch, img in rows:
        w, h = img.size if img else (0, 0)
        if x + w > W:
            x, y, row_h = 0, y + row_h + 1, 0
        place.append((x, y))
        x += w + 1
        row_h = max(row_h, h)
    atlas = Image.new("L", (W, y + row_h + 1), 0)
    k = 0
    for fi, info in enumerate(meta["faces"]):
        for g in info["glyphs"]:
            gx, gy = place[k]
            img = rows[k][2]
            if img:
                atlas.paste(img, (gx, gy))
            g["x"], g["y"] = gx, gy
            k += 1
    atlas.save(os.path.join(out, "ui-font.png"))
    with open(os.path.join(out, "ui-font.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(meta, f, indent=1)
    print(f"font-rasterize: {len(FACES)} faces, atlas {W}x{atlas.size[1]} -> {out}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""LiteKern X - turn screenshots from tests/lib/drive.py into an animated GIF
(for showing animations; needs Pillow).

    python tools/frames2gif.py OUT.gif WIDTH MS FRAME.png...

WIDTH: scale to this many pixels wide (0 keeps the size). MS: per frame;
the last frame is held for a second. Identical frames in a row are merged.
"""
import sys

from PIL import Image


def main():
    out, width, ms, files = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4:]
    frames, durations, last = [], [], None
    for f in files:
        im = Image.open(f).convert("RGB")
        if width and im.width != width:
            im = im.resize((width, im.height * width // im.width), Image.LANCZOS)
        data = im.tobytes()
        if data == last:
            durations[-1] += ms
            continue
        last = data
        frames.append(im)
        durations.append(ms)
    durations[-1] += 1000
    pal = [f.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG) for f in frames]
    pal[0].save(out, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True)
    print(f"{out}: {len(frames)} frames")


if __name__ == "__main__":
    main()

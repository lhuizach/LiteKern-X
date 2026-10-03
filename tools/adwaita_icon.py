"""LiteKern X — shared drawing code for the 1024x1024 Adwaita-style app icons
(tools/make-icon-*.py), so every icon gets the same shapes, light and checks.

Shapes are signed distance fields (negative inside), so edges are
anti-aliased exactly with no supersampling. Paint them back to front onto an
Icon, then save() it: that writes the transparent PNG plus a 48x48 preview on
the desktop slate (#202634) to check it reads when small. Needs numpy and
Pillow.
"""
import os

import numpy as np
from PIL import Image

N = 1024
BODY = 0.83 * N                         # the icon's longer side: 850 px
MARGIN = (N - BODY) / 2                 # 87

yy, xx = np.mgrid[0:N, 0:N].astype(np.float64) + 0.5


def rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)])


def rounded_rect(x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = np.abs(xx - cx) - hx, np.abs(yy - cy) - hy
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    inside = np.minimum(np.maximum(qx, qy), 0)
    return outside + inside - r


def circle(cx, cy, r):
    return np.hypot(xx - cx, yy - cy) - r


def polygon(pts):
    """Signed distance to a convex polygon given clockwise (screen coords)."""
    pts = np.asarray(pts, float)
    d = np.full(xx.shape, np.inf)
    sign = np.ones(xx.shape)
    inside = np.ones(xx.shape, bool)
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        e = b - a
        wx, wy = xx - a[0], yy - a[1]
        t = np.clip((wx * e[0] + wy * e[1]) / (e @ e), 0, 1)
        d = np.minimum(d, np.hypot(wx - e[0] * t, wy - e[1] * t))
        inside &= (e[0] * wy - e[1] * wx) >= 0
    sign[inside] = -1
    return d * sign


def smooth_union(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def coverage(sdf):
    return np.clip(0.5 - sdf, 0, 1)


def lit(colour, sdf, top, bottom):
    """The shape's colour with soft top light: about 4% lighter at the top,
    and a darker band along the bottom edge."""
    t = np.clip((yy - top) / (bottom - top), 0, 1)[..., None]
    c = colour * (1.04 - 0.06 * t) + (1 - colour) * 0.04 * (1 - t)
    edge = np.clip((yy - (bottom - 22)) / 22, 0, 1)[..., None] * (sdf > -60)[..., None]
    return np.clip(c * (1 - 0.14 * edge), 0, 1)


class Icon:
    def __init__(self):
        self.img = np.zeros((N, N, 3))
        self.alpha = np.zeros((N, N))

    def paint(self, sdf, colour, top=None, bottom=None):
        """Paint a shape. With top/bottom (the shape's extent) it gets the
        soft top light; without, `colour` is used as given."""
        if top is not None:
            colour = lit(colour, sdf, top, bottom)
        a = coverage(sdf)[..., None]
        self.img = self.img * (1 - a) + colour * a
        self.alpha = self.alpha + a[..., 0] * (1 - self.alpha)

    def rgba(self):
        rgba = np.dstack([self.img / np.maximum(self.alpha, 1e-9)[..., None], self.alpha])
        rgba[self.alpha == 0] = 0
        return np.clip(np.round(rgba * 255), 0, 255).astype(np.uint8)

    def save(self, out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        icon = Image.fromarray(self.rgba(), "RGBA")
        icon.save(out, "PNG", optimize=True)

        small = icon.resize((48, 48), Image.LANCZOS)
        preview = Image.new("RGBA", (48, 48), (0x20, 0x26, 0x34, 255))
        preview.alpha_composite(small)
        prev_path = os.path.splitext(out)[0] + "-preview48.png"
        preview.convert("RGB").save(prev_path)

        # The icon the build uses (assets/icons/<name>.png, listed in
        # assets/icons.json), when this is one of the app icons' sources.
        base = os.path.basename(out)
        if base.endswith("-1024.png") and os.path.basename(os.path.dirname(out)) == "source":
            app = os.path.join(os.path.dirname(os.path.dirname(out)), base[:-len("-1024.png")] + ".png")
            small.save(app, "PNG", optimize=True)
            print(f"app icon: {os.path.normpath(app)} (48x48)")

        a = np.asarray(icon)[..., 3]
        ys, xs = np.nonzero(a)
        w, h = xs.max() - xs.min() + 1, ys.max() - ys.min() + 1
        print(f"{os.path.normpath(out)}: 1024x1024 RGBA, {os.path.getsize(out) / 1024:.0f} KB")
        print(f"body {xs.min()}..{xs.max()} x {ys.min()}..{ys.max()} "
              f"({max(w, h) / N:.0%} of the canvas), corners transparent: {a[0, 0] == 0}")
        print(f"preview: {os.path.normpath(prev_path)}")


def source_path(name):
    return os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "source", f"{name}-1024.png")

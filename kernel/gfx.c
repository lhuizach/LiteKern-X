#include "kernel/gfx.h"
#include "kernel/font.h"

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }

int gfx_rect_empty(struct gfx_rect r)
{
    return r.w <= 0 || r.h <= 0;
}

struct gfx_rect gfx_rect_intersect(struct gfx_rect a, struct gfx_rect b)
{
    int x0 = max(a.x, b.x), y0 = max(a.y, b.y);
    int x1 = min(a.x + a.w, b.x + b.w), y1 = min(a.y + a.h, b.y + b.h);
    struct gfx_rect r = { x0, y0, x1 - x0, y1 - y0 };
    if (gfx_rect_empty(r))
        r.w = r.h = 0;
    return r;
}

struct gfx_rect gfx_rect_union(struct gfx_rect a, struct gfx_rect b)
{
    if (gfx_rect_empty(a))
        return b;
    if (gfx_rect_empty(b))
        return a;
    int x0 = min(a.x, b.x), y0 = min(a.y, b.y);
    int x1 = max(a.x + a.w, b.x + b.w), y1 = max(a.y + a.h, b.y + b.h);
    return (struct gfx_rect){ x0, y0, x1 - x0, y1 - y0 };
}

/* The part of (x, y, w, h) inside the surface. */
static struct gfx_rect clip(const struct gfx_surface *s, int x, int y, int w, int h)
{
    return gfx_rect_intersect((struct gfx_rect){ x, y, w, h }, (struct gfx_rect){ 0, 0, s->w, s->h });
}

void gfx_fill_rect(struct gfx_surface *s, int x, int y, int w, int h, uint32_t colour)
{
    struct gfx_rect r = clip(s, x, y, w, h);
    for (int row = 0; row < r.h; row++) {
        uint32_t *p = s->px + (r.y + row) * s->stride + r.x;
        for (int col = 0; col < r.w; col++)
            p[col] = colour;
    }
}

void gfx_rect_outline(struct gfx_surface *s, int x, int y, int w, int h, uint32_t colour)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_fill_rect(s, x, y, w, 1, colour);
    gfx_fill_rect(s, x, y + h - 1, w, 1, colour);
    gfx_fill_rect(s, x, y + 1, 1, h - 2, colour);
    gfx_fill_rect(s, x + w - 1, y + 1, 1, h - 2, colour);
}

void gfx_line(struct gfx_surface *s, int x0, int y0, int x1, int y1, uint32_t colour)
{
    if (x0 == x1 || y0 == y1) {     /* straight: one clipped fill */
        gfx_fill_rect(s, min(x0, x1), min(y0, y1), (x0 > x1 ? x0 - x1 : x1 - x0) + 1,
                      (y0 > y1 ? y0 - y1 : y1 - y0) + 1, colour);
        return;
    }
    /* Bresenham, clipping per pixel. */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && x0 < s->w && y0 < s->h)
            s->px[y0 * s->stride + x0] = colour;
        if (x0 == x1 && y0 == y1)
            return;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_blit(struct gfx_surface *dst, int dx, int dy,
              const struct gfx_surface *src, int sx, int sy, int w, int h)
{
    /* Clip against the source, then the destination, keeping both in step. */
    struct gfx_rect rs = gfx_rect_intersect((struct gfx_rect){ sx, sy, w, h },
                                            (struct gfx_rect){ 0, 0, src->w, src->h });
    dx += rs.x - sx;
    dy += rs.y - sy;
    struct gfx_rect rd = clip(dst, dx, dy, rs.w, rs.h);
    int ox = rs.x + (rd.x - dx), oy = rs.y + (rd.y - dy);
    for (int row = 0; row < rd.h; row++) {
        uint32_t *d = dst->px + (rd.y + row) * dst->stride + rd.x;
        const uint32_t *p = src->px + (oy + row) * src->stride + ox;
        for (int col = 0; col < rd.w; col++)
            d[col] = p[col];
    }
}

/* (a * alpha + b * (255 - alpha)) / 255 for one 8-bit channel, rounded. */
static uint32_t mix(uint32_t a, uint32_t b, uint32_t alpha)
{
    uint32_t v = a * alpha + b * (255 - alpha) + 128;
    return (v + (v >> 8)) >> 8;
}

void gfx_blit_alpha(struct gfx_surface *dst, int dx, int dy,
                    const uint32_t *argb, int w, int h, int stride)
{
    struct gfx_rect rd = clip(dst, dx, dy, w, h);
    for (int row = 0; row < rd.h; row++) {
        uint32_t *d = dst->px + (rd.y + row) * dst->stride + rd.x;
        const uint32_t *p = argb + (rd.y - dy + row) * stride + (rd.x - dx);
        for (int col = 0; col < rd.w; col++) {
            uint32_t s = p[col], a = s >> 24;
            if (a == 255) {
                d[col] = s & 0xffffff;
            } else if (a) {
                uint32_t o = d[col];
                d[col] = mix(s >> 16 & 0xff, o >> 16 & 0xff, a) << 16 |
                         mix(s >> 8 & 0xff, o >> 8 & 0xff, a) << 8 |
                         mix(s & 0xff, o & 0xff, a);
            }
        }
    }
}

void gfx_char(struct gfx_surface *s, int x, int y, char c, uint32_t fg, uint32_t bg)
{
    struct gfx_rect r = clip(s, x, y, FONT_W, FONT_H);
    const uint8_t *glyph = font_glyph((uint8_t)c);
    for (int row = r.y; row < r.y + r.h; row++) {
        uint8_t bits = glyph[row - y];
        uint32_t *p = s->px + row * s->stride;
        for (int col = r.x; col < r.x + r.w; col++) {
            if (bits & (0x80 >> (col - x)))
                p[col] = fg;
            else if (bg != GFX_TRANSPARENT)
                p[col] = bg;
        }
    }
}

int gfx_text(struct gfx_surface *s, int x, int y, const char *str, uint32_t fg, uint32_t bg)
{
    int x0 = x;
    for (; *str; str++, x += FONT_W)
        gfx_char(s, x, y, *str, fg, bg);
    return x - x0;
}

int gfx_text_width(const char *str)
{
    int n = 0;
    while (str[n])
        n++;
    return n * FONT_W;
}

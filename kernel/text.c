#include "kernel/text.h"

static const struct text_glyph *glyph(const struct text_face *f, char c)
{
    unsigned char u = (unsigned char)c;
    if (u >= 0x80 && u < 0x80 + TEXT_EXTRA)
        return &f->glyphs[95 + (u - 0x80)];
    return &f->glyphs[(u >= 32 && u <= 126 ? u : '?') - 32];
}

/* (a * alpha + b * (255 - alpha)) / 255, per channel, rounded. */
static uint32_t blend(uint32_t a, uint32_t b, uint32_t alpha)
{
    uint32_t rb = (a & 0xff00ff) * alpha + (b & 0xff00ff) * (255 - alpha) + 0x800080;
    uint32_t g = (a & 0x00ff00) * alpha + (b & 0x00ff00) * (255 - alpha) + 0x008000;
    rb = (rb + ((rb >> 8) & 0xff00ff)) >> 8 & 0xff00ff;
    g = (g + ((g >> 8) & 0x00ff00)) >> 8 & 0x00ff00;
    return rb | g;
}

static void draw_glyph(struct gfx_surface *s, int x, int y, const struct text_face *f,
                       const struct text_glyph *g, uint32_t colour)
{
    const uint8_t *cov = f->cov + g->off;
    for (int row = 0; row < g->h; row++) {
        int py = y + g->yoff + row;
        if (py < 0 || py >= s->h)
            continue;
        uint32_t *line = s->px + py * s->stride;
        for (int col = 0; col < g->w; col++) {
            int px = x + g->xoff + col;
            uint32_t a = cov[row * g->w + col];
            if (!a || px < 0 || px >= s->w)
                continue;
            line[px] = a == 255 ? colour : blend(colour, line[px], a);
        }
    }
}

int text_draw_n(struct gfx_surface *s, int x, int y, const char *str, int n, enum text_style st,
                uint32_t colour)
{
    const struct text_face *f = &text_faces[st];
    int pen64 = x * 64;
    for (int i = 0; i < n && str[i]; i++) {
        const struct text_glyph *g = glyph(f, str[i]);
        if (g->w)
            draw_glyph(s, (pen64 + 32) >> 6, y, f, g, colour);
        pen64 += g->adv64;
    }
    return ((pen64 + 32) >> 6) - x;
}

int text_draw(struct gfx_surface *s, int x, int y, const char *str, enum text_style st,
              uint32_t colour)
{
    return text_draw_n(s, x, y, str, 0x7fffffff, st, colour);
}

int text_width_n(const char *str, int n, enum text_style st)
{
    const struct text_face *f = &text_faces[st];
    int w64 = 0;
    for (int i = 0; i < n && str[i]; i++)
        w64 += glyph(f, str[i])->adv64;
    return (w64 + 32) >> 6;
}

int text_width(const char *str, enum text_style st)
{
    return text_width_n(str, 0x7fffffff, st);
}

int text_height(enum text_style st)
{
    return text_faces[st].ascent + text_faces[st].descent;
}

int text_index_at(const char *str, enum text_style st, int x)
{
    const struct text_face *f = &text_faces[st];
    int pen64 = 0, i = 0;
    for (; str[i]; i++) {
        int adv = glyph(f, str[i])->adv64;
        if (x * 64 < pen64 + adv / 2)
            return i;
        pen64 += adv;
    }
    return i;
}

int text_draw_fit(struct gfx_surface *s, int x, int y, const char *str, int max_w,
                  enum text_style st, uint32_t colour)
{
    if (text_width(str, st) <= max_w)
        return text_draw(s, x, y, str, st, colour);
    int dots = text_width(TEXT_ELLIPSIS, st), n = 0;
    while (str[n] && text_width_n(str, n + 1, st) + dots <= max_w)
        n++;
    while (n > 0 && str[n - 1] == ' ')
        n--;                            /* "Holiday ..." reads worse than "Holiday..." */
    int w = text_draw_n(s, x, y, str, n, st, colour);
    return w + text_draw(s, x + w, y, TEXT_ELLIPSIS, st, colour);
}

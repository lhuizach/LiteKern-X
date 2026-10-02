/* LiteKern X — software rendering into RAM surfaces (Phase 2 §1).
 *
 * Everything draws into a struct gfx_surface in RAM; nothing here touches the
 * framebuffer. The screen's back buffer is one such surface, and
 * kernel/screen.h copies only its changed areas to the display driver. That's
 * the v1 design (backbuffer + present_rect), ported down to the primitives
 * Phase 2 needs; v1's anti-aliased shapes, blur and proportional font are
 * Phase 3 polish.
 *
 * Coordinates are signed and everything is clipped to the surface, so a
 * shape partly or entirely off-surface is fine and never writes outside it.
 * Colours are 0x00RRGGBB. Images with transparency are 0xAARRGGBB, straight
 * (not premultiplied) alpha. */
#ifndef LKX_GFX_H
#define LKX_GFX_H

#include <stdint.h>

struct gfx_surface {
    uint32_t *px;
    int w, h;
    int stride;         /* pixels per row (>= w) */
};

struct gfx_rect {
    int x, y, w, h;
};

/* For gfx_char/gfx_text: draw the glyph's pixels only, leave the rest alone. */
#define GFX_TRANSPARENT 0xff000000u

/* Rectangles. */
int gfx_rect_empty(struct gfx_rect r);
struct gfx_rect gfx_rect_intersect(struct gfx_rect a, struct gfx_rect b);
struct gfx_rect gfx_rect_union(struct gfx_rect a, struct gfx_rect b);   /* bounding box */

/* Primitives. */
void gfx_fill_rect(struct gfx_surface *s, int x, int y, int w, int h, uint32_t colour);
void gfx_rect_outline(struct gfx_surface *s, int x, int y, int w, int h, uint32_t colour);
void gfx_line(struct gfx_surface *s, int x0, int y0, int x1, int y1, uint32_t colour);

/* Solid shapes with hard (not anti-aliased) edges; Phase 3 adds smoothing. */
void gfx_fill_round_rect(struct gfx_surface *s, int x, int y, int w, int h, int r, uint32_t colour);
void gfx_fill_circle(struct gfx_surface *s, int cx, int cy, int r, uint32_t colour);

/* Paint `outside` over the four corners of (x, y, w, h) beyond a radius-r
 * curve: rounds a card after drawing its contents square. */
void gfx_round_corners(struct gfx_surface *s, int x, int y, int w, int h, int r, uint32_t outside);

/* A surface that is part of s (clipped to it): drawing into it is clipped
 * to r and uses r's top-left as (0, 0). */
struct gfx_surface gfx_sub(const struct gfx_surface *s, struct gfx_rect r);

/* Colour a over b at alpha (0..255), e.g. Adwaita's "white at 10%". */
uint32_t gfx_mix(uint32_t a, uint32_t b, uint32_t alpha);
/* Blend `colour` at alpha (0..255) over a rounded rectangle: translucent
 * shapes over a picture, e.g. the dock over the wallpaper. */
void gfx_blend_round_rect(struct gfx_surface *s, int x, int y, int w, int h, int r,
                          uint32_t colour, uint32_t alpha);
/* Blend a rectangle towards black (alpha 0..255): dims what's behind a dialog. */
void gfx_darken(struct gfx_surface *s, int x, int y, int w, int h, uint32_t alpha);

/* Copy a w x h block from src (at sx, sy) to dst (at dx, dy). Opaque. */
void gfx_blit(struct gfx_surface *dst, int dx, int dy,
              const struct gfx_surface *src, int sx, int sy, int w, int h);

/* Composite a w x h ARGB image onto dst at (dx, dy) using its alpha. */
void gfx_blit_alpha(struct gfx_surface *dst, int dx, int dy,
                    const uint32_t *argb, int w, int h, int stride);

/* Text in the 8x16 bitmap font (kernel/font.h). bg may be GFX_TRANSPARENT.
 * gfx_text returns the width drawn; '\n' is not special. */
void gfx_char(struct gfx_surface *s, int x, int y, char c, uint32_t fg, uint32_t bg);
int gfx_text(struct gfx_surface *s, int x, int y, const char *str, uint32_t fg, uint32_t bg);
int gfx_text_width(const char *str);

/* --- Phase 3 §6 ------------------------------------------------------------------ */

/* A view: a surface showing the screen from (ox, oy), so something at screen
 * (x, y) is drawn at (x - ox, y - oy). The compositor draws each damaged
 * rectangle through one; gfx_view_clip narrows one to r (screen
 * coordinates), keeping it consistent. */
struct gfx_view {
    struct gfx_surface s;
    int ox, oy;
};
struct gfx_view gfx_view_clip(const struct gfx_view *v, struct gfx_rect r);

/* Copy src's sr scaled to fill d (nearest pixel), blended at alpha
 * (0..255), with hard-edged rounded corners of `radius` (0: square). For
 * animations: quick, not pretty up close. */
void gfx_blit_scaled(struct gfx_surface *dst, struct gfx_rect d, const struct gfx_surface *src,
                     struct gfx_rect sr, uint32_t alpha, int radius);
/* An ARGB image scaled to d, its alpha times `alpha` (0..255). */
void gfx_blit_alpha_scaled(struct gfx_surface *dst, struct gfx_rect d, const uint32_t *argb, int w,
                           int h, int stride, uint32_t alpha);
/* A soft drop shadow around (x, y, w, h) (radius r), `size` px wide,
 * darkest at `strength` (0..255). Only drawn outside the rectangle. */
void gfx_shadow(struct gfx_surface *s, int x, int y, int w, int h, int r, int size, uint32_t strength);

#endif

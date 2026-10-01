/* LiteKern X — the GUI's text: a proportional, anti-aliased font (Phase 3
 * §5), Ubuntu Sans rasterised at build time (assets/fonts/, tools/font2c.py).
 * The console keeps the 8x16 bitmap font (kernel/font.h).
 *
 * Each style is one size and weight. Text is drawn with its line box's top
 * at y: the box is text_height() tall, and the glyphs are blended into the
 * surface by their coverage (straight alpha over whatever is there). Printable
 * ASCII, plus a few symbols as the bytes below; other bytes draw as '?'. */
#ifndef LKX_TEXT_H
#define LKX_TEXT_H

#include <stdint.h>
#include "kernel/gfx.h"

#define TEXT_MINUS    "\x80"     /* U+2212 */
#define TEXT_TIMES    "\x81"     /* U+00D7 */
#define TEXT_DIVIDE   "\x82"     /* U+00F7 */
#define TEXT_ELLIPSIS "\x83"     /* U+2026 */
#define TEXT_EXTRA    4

enum text_style {
    TEXT_BODY,          /* 15 px regular: most text */
    TEXT_BOLD,          /* 15 px bold: buttons, titles */
    TEXT_SMALL,         /* 13 px regular: captions */
    TEXT_HEADING,       /* 20 px bold: dialog and page headings */
    TEXT_LARGE,         /* 32 px light: big numbers (Calculator) */
    TEXT_FACES
};

struct text_glyph {
    uint8_t w, h;
    int8_t xoff, yoff;  /* from the pen position and the line box's top */
    uint16_t adv64;     /* advance, 1/64 px */
    uint32_t off;       /* into the face's coverage */
};

struct text_face {
    const char *name;
    int size, ascent, descent;
    const struct text_glyph *glyphs;    /* for ' ' .. '~', then the TEXT_EXTRA symbols */
    const uint8_t *cov;
};

/* Generated (build/gen/uifont.c). */
extern const struct text_face text_faces[TEXT_FACES];

/* Draw; returns the width drawn. */
int text_draw(struct gfx_surface *s, int x, int y, const char *str, enum text_style st,
              uint32_t colour);
/* The first n characters only. */
int text_draw_n(struct gfx_surface *s, int x, int y, const char *str, int n, enum text_style st,
                uint32_t colour);
/* Draw in at most max_w pixels, ending in an ellipsis if it doesn't fit. */
int text_draw_fit(struct gfx_surface *s, int x, int y, const char *str, int max_w,
                  enum text_style st, uint32_t colour);

int text_width(const char *str, enum text_style st);
int text_width_n(const char *str, int n, enum text_style st);
int text_height(enum text_style st);        /* the line box */
/* The character boundary (0..strlen) nearest to x pixels into the text. */
int text_index_at(const char *str, enum text_style st, int x);

#endif

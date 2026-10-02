/* LiteKern X — the mouse cursor (Phase 2 §2).
 *
 * Shapes come from the PNGs in assets/cursors/ + assets/cursors.json, turned into C
 * at build time by tools/cursors2c.py (rules: docs/ASSET-PROMPTS.md §3). The
 * cursor is the screen's overlay (kernel/screen.h): composited over the back
 * buffer on its way to the display, never drawn into it, so moving it costs
 * two 32x32 updates and nothing under it is ever lost. */
#ifndef LKX_CURSOR_H
#define LKX_CURSOR_H

#include <stdint.h>

struct cursor_shape {
    const char *name;
    int w, h;
    int hot_x, hot_y;               /* the pixel that "clicks" */
    int frames;                     /* > 1: animated */
    int frame_ms;
    const uint32_t *const *px;      /* frames x (w*h) pixels, 0xAARRGGBB */
};

/* Generated from assets/cursors.json. */
extern const struct cursor_shape cursor_shapes[];
extern const int cursor_shape_count;

const struct cursor_shape *cursor_find(const char *name);

/* Show the "arrow" cursor at the centre of the screen. Needs the screen
 * (kernel/screen.h). 0 or -ENODEV. */
int cursor_init(void);

/* Put the hotspot at (x, y), clamped to the screen, and present. */
void cursor_move_to(int x, int y);
int cursor_x(void);
int cursor_y(void);

/* 0 or -ENODEV if there is no cursor of that name. */
int cursor_set_shape(const char *name);

/* Put the cursor back on the screen's overlay (after something else used it). */
void cursor_refresh(void);

/* Hide it for good (restarting, switching off). */
void cursor_hide(void);

#endif

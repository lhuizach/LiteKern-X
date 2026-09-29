/* LiteKern X — the screen: a back buffer in RAM plus damage tracking
 * (Phase 2 §1).
 *
 * Draw into screen_surface() with kernel/gfx.h, report what changed with
 * screen_damage(), and screen_present() copies only those areas to the
 * display driver (fb0, FB_BLIT). Full-screen redraws every frame are what
 * the roadmap rules out for Atom-class hardware; a moving cursor, a pressed
 * button or a blinking caret cost only their own rectangle.
 *
 * Damage is a short list of rectangles. Overlapping or touching ones merge;
 * when the list is full everything collapses into one bounding box, which is
 * never wrong, just a little more copying. */
#ifndef LKX_SCREEN_H
#define LKX_SCREEN_H

#include "kernel/driver.h"
#include "kernel/gfx.h"

#define SCREEN_MAX_DAMAGE 16

/* Allocate the back buffer for fb0's mode. 0, -ENODEV or -ENOMEM. */
int screen_init(device_t *fb);
int screen_ready(void);

struct gfx_surface *screen_surface(void);

void screen_damage(int x, int y, int w, int h);
void screen_damage_all(void);

/* Copy the damaged areas to the display and clear the list. Returns the
 * number of pixels copied. */
uint32_t screen_present(void);

/* The pending damage, for tests and diagnostics. */
int screen_damage_count(void);
struct gfx_rect screen_damage_rect(int i);

#endif

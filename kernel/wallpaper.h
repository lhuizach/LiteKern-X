/* LiteKern X — the desktop wallpaper (brought forward from Phase 3 §5 on
 * 2026-09-30).
 *
 * The image is packed into the kernel at build time (tools/wallpaper-pack.py,
 * from WALLPAPER in the Makefile) and unpacked once at boot into a buffer
 * the size of the screen: the image drawn 1:1 and centred, and any space
 * around it (the VMs' 1024x768 screen) filled with its edge colours, as
 * docs/ASSET-PROMPTS.md §8 says. Without a valid image, the theme's plain
 * desktop colour is used instead. */
#ifndef LKX_WALLPAPER_H
#define LKX_WALLPAPER_H

#include <stdint.h>
#include "kernel/gfx.h"

/* Unpack and check the built-in wallpaper (needs the screen). Logs what it
 * did; 0, or a negative error (the plain colour is used then). */
int wallpaper_init(void);

/* Draw the background into the screen's (x, y, w, h), darkened by `dim`
 * (0..255, 0 = not at all). */
void wallpaper_draw(int x, int y, int w, int h, uint32_t dim);

/* The background colour at the screen's bottom-right corner (for tests and
 * for anything that needs one colour to stand for the background). */
uint32_t wallpaper_corner(void);

#endif

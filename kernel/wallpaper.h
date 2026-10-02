/* LiteKern X — the desktop wallpaper (Phase 2, extended in Phase 3 §5).
 *
 * Wallpapers are in the ramdisk as wallpapers/<name>.lkxw (packed at build
 * time by tools/wallpaper-pack.py from assets/wallpapers/<name>.png), with
 * an optional <name>-light used by the light style. The one shown is
 * unpacked into a buffer the size of the screen: drawn 1:1 and centred, any
 * space around it (the VMs' 1024x768) filled with its edge colours
 * (docs/ASSET-PROMPTS.md §8). Without one, the theme's plain desktop colour
 * is used. */
#ifndef LKX_WALLPAPER_H
#define LKX_WALLPAPER_H

#include <stdint.h>
#include "kernel/gfx.h"

/* Make the buffer and show `name` (needs the screen and the ramdisk).
 * Logs what it did; 0, or a negative error (the plain colour is used). */
int wallpaper_init(const char *name);

/* Show another one (0, -ENOENT, -EINVAL if damaged); the old one stays on
 * failure. wallpaper_refresh() reloads the current one, e.g. after the
 * style changed (for its -light variant). */
int wallpaper_set(const char *name);
int wallpaper_refresh(void);
const char *wallpaper_current(void);       /* "" for the plain colour */

/* The wallpapers there are (light variants not listed separately). */
int wallpaper_list(char names[][32], int max);

/* A w x h preview of one, 0x00RRGGBB, for the Settings app. */
int wallpaper_thumb(const char *name, uint32_t *out, int w, int h);

/* Draw the background into dst, which shows the screen from (ox, oy),
 * darkened by `dim` (0..255, 0 = not at all). */
void wallpaper_draw(struct gfx_surface *dst, int ox, int oy, uint32_t dim);

/* The background colour at the screen's bottom-right corner (for tests). */
uint32_t wallpaper_corner(void);

#endif

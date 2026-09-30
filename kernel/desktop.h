/* LiteKern X — the desktop (Phase 2): what shows after boot.
 *
 * GNOME-style and minimal (Phase 3 redoes it): a black top bar, and an app
 * grid of icons with names on a plain Adwaita-dark background. Clicking an
 * app opens it full screen (kernel/wm.h); closing it comes back here. */
#ifndef LKX_DESKTOP_H
#define LKX_DESKTOP_H

#include <stdint.h>

/* Draw the whole desktop (unless a window is open) and present. */
void desktop_show(void);

/* Pointer input while no window is open: hover highlights a tile, a click
 * (press and release on the same tile) opens that app. */
void desktop_input_mouse(int x, int y, uint8_t buttons);

/* Hand the open window's events to its app. The close button closes the
 * app and shows the desktop. Events of a window no app owns (the self-tests
 * open one) are logged. */
void desktop_handle_events(void);

#endif

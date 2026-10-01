/* LiteKern X — the shell (Phase 2): what shows after boot.
 *
 * GNOME-style and minimal (Phase 3 redoes it):
 *   - the top bar, always: Home and the open app's name on the left, the
 *     day, date and time (24-hour, from rtc0) in the middle, a power menu
 *     (Restart; Shut Down waits for ACPI in Phase 5) on the right
 *   - the desktop, while no app is open: the app grid, and a dock of the
 *     same apps at the bottom
 * Clicking an app opens it full screen below the top bar (kernel/wm.h);
 * its close button or Home comes back here. */
#ifndef LKX_DESKTOP_H
#define LKX_DESKTOP_H

#include <stdint.h>
#include "kernel/input.h"

/* After boot: draw the top bar, and the desktop unless a window is already
 * open (the self-tests open one). */
void desktop_start(void);

/* Redraw the whole desktop (unless a window is open) and present. */
void desktop_show(void);

/* The theme or wallpaper changed: redraw the top bar (and the desktop, if
 * it's showing). */
void desktop_refresh(void);

/* Pointer input, every packet. Returns 1 if the shell took it (the top bar,
 * the power menu, the desktop); 0 means it's the open window's. */
int desktop_input_mouse(int x, int y, uint8_t buttons);

/* Keys the shell takes (Esc closes the power menu). 1 if taken. */
int desktop_input_key(const struct key_event *k);

/* Call often: once a second (rtc0's tick) it updates the clock if the
 * minute changed. */
void desktop_tick(void);

/* Hand the open window's events to its app. The close button closes the
 * app and shows the desktop. Events of a window no app owns (the self-tests
 * open one) are logged. */
void desktop_handle_events(void);

#endif

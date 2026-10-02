/* LiteKern X — the shell: what shows after boot (Phase 2; windows and the
 * new dock in Phase 3 §6).
 *
 *   - the top bar, always: Home (shows the desktop: minimises every window)
 *     and the focused app's name on the left, the day, date and time in the
 *     middle, a power menu (Restart; Shut Down, through ACPI: kernel/acpi.h) on
 *     the right
 *   - the wallpaper, with the apps' windows floating over it (kernel/wm.h)
 *   - the dock: the favourite apps (pinned), then a line and any other app
 *     that's open, then Show Apps (the app menu: a grid of every app). A dot
 *     under an app says it's open: grey when minimised, white when on screen,
 *     a long pill for the focused one. While a maximised window is in front
 *     the dock hides; pushing the pointer against the bottom edge brings it
 *     back until the pointer leaves it.
 *
 * Everything moves on springs, so it overshoots a little and settles: the
 * dock's icons sliding and popping in and out, its dots, the dock rising.
 * Windows grow out of the icon that opened them, shrink into the dock when
 * minimised and back out when restored, and fade away when closed.
 *
 * The shell is also the compositor: shell_damage() marks part of the screen
 * changed and shell_flush() redraws those parts from the bottom up
 * (wallpaper, windows, dock, top bar, menus) and shows them. */
#ifndef LKX_DESKTOP_H
#define LKX_DESKTOP_H

#include <stdint.h>
#include "kernel/input.h"

struct app;
struct wm_window;

/* After boot: the apps, the clock and the wallpaper, then the desktop. */
void desktop_start(void);

/* The theme or wallpaper changed: redraw everything. */
void desktop_refresh(void);

/* Pointer input, every packet: the shell's (top bar, menus, dock, app menu)
 * or passed on to the windows. */
void desktop_input_mouse(int x, int y, uint8_t buttons);

/* Keys the shell takes (Esc closes the power menu or the app menu). 1 if
 * taken. */
int desktop_input_key(const struct key_event *k);

/* Call often: the clock (once a second, rtc0's tick) and the dock's
 * animations. */
void desktop_tick(void);
/* 1 while something is animating: the input loop mustn't sleep. */
int desktop_busy(void);

/* Hand the kernel apps' window events (Log's) to them; a window no app owns
 * (the self-tests open one) has its events logged. */
void desktop_handle_events(void);

/* A ring 3 app ended (kernel/lkx.c): its window closes. */
void desktop_app_ended(const struct app *a, struct wm_window *w);

#endif

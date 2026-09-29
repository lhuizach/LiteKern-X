/* LiteKern X — the window system (Phase 2 §3): one app at a time, full screen
 * (decided 2026-09-30), in the Adwaita dark style (kernel/theme.h).
 *
 * The window is the whole screen: an Adwaita header bar (title in the
 * middle, the app's buttons on either side, the round close button on the
 * right) over the app's content area. The app draws into wm_content() (in
 * content coordinates, clipped to it) and calls wm_present(). Input arrives
 * as events: clicks and keys for the content, presses of the app's header
 * buttons, and a request to close. Opening a window replaces the current
 * one; closing it brings the boot log back. */
#ifndef LKX_WM_H
#define LKX_WM_H

#include <stdint.h>
#include "kernel/gfx.h"
#include "kernel/input.h"

#define WM_MAX_BUTTONS 6
#define WM_TITLE_MAX   48

enum wm_icon {
    WM_ICON_NONE,       /* a text button */
    WM_ICON_BACK,       /* < */
    WM_ICON_ADD,        /* + */
    WM_ICON_UP,         /* ^ (parent folder) */
};

enum wm_side { WM_LEFT, WM_RIGHT };

enum wm_event_type {
    WM_EVENT_KEY,           /* key: a key was pressed */
    WM_EVENT_CLICK,         /* x, y (content coordinates), buttons: a button went down */
    WM_EVENT_HEADER,        /* id: one of the app's header buttons was pressed */
    WM_EVENT_CLOSE,         /* the close button was pressed: the app should wm_close() */
};

struct wm_event {
    enum wm_event_type type;
    int x, y;
    uint8_t buttons;
    int id;
    struct key_event key;
};

/* Open the (one) window, replacing any other. The console is hidden. */
void wm_open(const char *title);
/* Close it: the console (boot log) comes back. */
void wm_close(void);
int wm_is_open(void);

void wm_set_title(const char *title);
/* Add a header-bar button: an icon, or a short text label. id comes back in
 * WM_EVENT_HEADER. Returns 0 or -ENOMEM (WM_MAX_BUTTONS). */
int wm_add_button(enum wm_side side, enum wm_icon icon, const char *label, int id);
void wm_clear_buttons(void);

/* The app's drawing area: everything below the header bar. */
struct gfx_surface *wm_content(void);
/* Mark part of the content (content coordinates) changed / show changes. */
void wm_damage(int x, int y, int w, int h);
void wm_present(void);

/* Next event for the app; 0 if none. */
int wm_poll_event(struct wm_event *ev);

/* From the input loop. Coordinates are the screen's. */
void wm_input_key(const struct key_event *key);
void wm_input_mouse(int x, int y, uint8_t buttons);

#endif

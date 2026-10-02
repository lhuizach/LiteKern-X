/* LiteKern X — the window system (Phase 2 §3; floating windows since
 * Phase 3 §6), in the Adwaita style (kernel/theme.h).
 *
 * Windows float over the desktop, Windows-like: each has a header bar (the
 * app's buttons on the left, its name in the middle, then minimise,
 * maximise and close on the right), can be dragged by its header bar,
 * resized from its edges and corners, maximised (the button, a double-click
 * on the header bar, or dragging it to the top of the screen), snapped to
 * half the screen (dragged to the left or right edge) and minimised into
 * the dock. Clicking one brings it to the front; keys go to the one in
 * front (the focused window).
 *
 * Each window keeps its own pixels: a buffer big enough for the largest
 * size it can take (the whole space below the top bar), so moving,
 * covering and uncovering it never asks the app to draw again; only a
 * resize does (WM_EVENT_RESIZE). The shell (kernel/desktop.h) composites
 * the windows with everything else. Apps draw into wm_content() and call
 * wm_present(). */
#ifndef LKX_WM_H
#define LKX_WM_H

#include <stdint.h>
#include "kernel/gfx.h"
#include "kernel/input.h"

#define WM_MAX           8
#define WM_MAX_BUTTONS   6
#define WM_TITLE_MAX     48
#define WM_RADIUS        12     /* window corners, when not maximised */
#define WM_MIN_W         300
#define WM_MIN_H         200

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
    WM_EVENT_CLOSE,         /* the close button was pressed: the app should end */
    WM_EVENT_POINTER,       /* x, y (content coordinates), buttons held, changed: the
                             * pointer moved or a button changed. Moves are merged
                             * while nothing changes. When the pointer leaves the
                             * window one more comes, with x, y outside the content. */
    WM_EVENT_THEME,         /* the style or accent changed: redraw */
    WM_EVENT_RESIZE,        /* x, y: the content's new width and height: lay out and
                             * redraw (the content surface already has the new size) */
};

struct wm_event {
    enum wm_event_type type;
    int x, y;
    uint8_t buttons;
    uint8_t changed;        /* POINTER: MOUSE_* bits that went down or up */
    int id;
    uint32_t time_ms;       /* when it happened (uptime), for double-clicks */
    struct key_event key;
};

struct wm_window;

/* Open a window of w x h (the whole window, header bar included), centred
 * below the top bar (moved along if another window is there), in front
 * and focused. min_w / min_h: the smallest it may be resized to (0: the
 * defaults). NULL if there's no room (WM_MAX) or memory. */
struct wm_window *wm_create(const char *title, int w, int h, int min_w, int min_h);
/* Close it for good (the shell animates it first). */
void wm_destroy(struct wm_window *w);

void wm_set_title(struct wm_window *w, const char *title);
const char *wm_title(const struct wm_window *w);
/* Add a header-bar button: an icon, or a short text label. id comes back in
 * WM_EVENT_HEADER. Returns 0 or -ENOMEM (WM_MAX_BUTTONS). */
int wm_add_button(struct wm_window *w, enum wm_side side, enum wm_icon icon, const char *label, int id);
void wm_clear_buttons(struct wm_window *w);

/* Who owns it (the shell's app), set by whoever created it. */
void wm_set_owner(struct wm_window *w, const void *owner);
const void *wm_owner(const struct wm_window *w);

/* The app's drawing area: everything inside the frame below the header
 * bar. Its stride is the buffer's (fixed); w and h follow the window. */
struct gfx_surface *wm_content(struct wm_window *w);
/* The whole buffer: its physical address and size in bytes (mapped into the
 * app as its canvas). */
uint32_t *wm_buffer(struct wm_window *w, uint32_t *bytes);
/* Mark part of the content (content coordinates) changed / show changes. */
void wm_damage(struct wm_window *w, int x, int y, int width, int height);
void wm_present(void);

/* Events. */
int wm_poll_event(struct wm_window *w, struct wm_event *ev);
int wm_has_event(const struct wm_window *w);
/* Queue a close request, as if the close button had been pressed. */
void wm_request_close(struct wm_window *w);
/* The theme changed: every window redraws its frame and is told. */
void wm_theme_changed(void);

/* State. */
enum wm_state { WM_NORMAL, WM_MAXIMISED, WM_MINIMISED };
struct gfx_rect wm_rect(const struct wm_window *w);         /* the window on screen */
struct gfx_rect wm_normal_rect(const struct wm_window *w);  /* where it goes when restored */
int wm_is_maximised(const struct wm_window *w);
int wm_is_minimised(const struct wm_window *w);
struct wm_window *wm_focused(void);
/* Windows bottom to top: wm_count() of them, wm_at(0) at the back. */
int wm_count(void);
struct wm_window *wm_at(int i);
/* The front window that isn't minimised, or NULL. */
struct wm_window *wm_top_visible(void);

/* Change state at once (the shell animates around these). */
void wm_focus(struct wm_window *w);         /* to the front, keys go to it */
void wm_set_minimised(struct wm_window *w, int minimised);
void wm_set_maximised(struct wm_window *w, int maximised);
void wm_move_resize(struct wm_window *w, struct gfx_rect r);
/* Leave it out of the picture (and of the pointer) while an animation
 * draws it instead. */
void wm_set_hidden(struct wm_window *w, int hidden);
/* 1 while the windows have the pointer to themselves: a drag, a resize, a
 * button or the content held down. */
int wm_grabbed(void);

/* The space windows live in: below the top bar (kernel/desktop.c sets it). */
void wm_set_area(struct gfx_rect area);
struct gfx_rect wm_area(void);

/* Drawing, for the compositor: every visible window, back to front, into
 * dst, which shows the screen from (ox, oy). wm_draw_one draws one at rect
 * r instead of its own (animations), without its shadow. */
void wm_draw(struct gfx_surface *dst, int ox, int oy);
void wm_draw_one(struct gfx_surface *dst, int ox, int oy, struct wm_window *w, struct gfx_rect r);
/* The area a window covers on screen, shadow included. */
struct gfx_rect wm_bounds(const struct wm_window *w);

/* From the input loop. Coordinates are the screen's. wm_input_mouse
 * returns 1 if a window took it (anything over a window, or a drag). */
void wm_input_key(const struct key_event *key);
int wm_input_mouse(int x, int y, uint8_t buttons);
/* The pointer left the windows for the shell (the dock, the top bar). */
void wm_pointer_gone(void);

/* What the shell provides (kernel/desktop.c): damage the screen (redrawn by
 * the next flush), redraw now, and what the minimise / maximise buttons do
 * (with their animations). */
void shell_damage(struct gfx_rect r);
void shell_flush(void);
void shell_window_minimise(struct wm_window *w);
void shell_window_maximise(struct wm_window *w, int maximise, struct gfx_rect from);
void shell_window_focus_changed(void);

#endif

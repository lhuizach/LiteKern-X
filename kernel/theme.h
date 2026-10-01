/* LiteKern X — the GUI theme: Adwaita (GNOME/Fedora), dark or light
 * (Phase 2 decided the look; Phase 3 §5 made it switchable).
 *
 * Colours are libadwaita's palettes, with its translucent colours
 * pre-blended over the surface they sit on, or blended at draw time with
 * `ink` (white in the dark style, black in the light one). No widget or app
 * hard-codes a colour: everything reads theme_get(). Changing the style or
 * accent rebuilds the theme; the shell redraws and apps get
 * K86_EVENT_THEME. Apps get their copy through SYS_THEME_GET (sdk/). */
#ifndef LKX_THEME_H
#define LKX_THEME_H

#include <stdint.h>

struct theme {
    uint32_t window_bg;         /* the window behind everything */
    uint32_t view_bg;           /* lists and content views */
    uint32_t headerbar_bg;
    uint32_t headerbar_border;  /* line under the header bar */
    uint32_t fg;                /* text */
    uint32_t fg_dim;            /* secondary text */
    uint32_t accent_bg;         /* suggested buttons, selection */
    uint32_t accent_fg;
    uint32_t accent;            /* accent-coloured text and icons */
    uint32_t border;            /* separators */
    uint32_t button_bg;         /* resting header-bar button */
    uint32_t button_hover;
    uint32_t button_active;     /* pressed */
    uint32_t row_hover;
    uint32_t row_selected;
    uint32_t warning;
    uint32_t destructive_bg;
    uint32_t destructive;       /* destructive text/icons */
    uint32_t dialog_bg;         /* dialogs and popovers */
    uint32_t desktop_bg;        /* the desktop without a wallpaper */
    uint32_t topbar_bg;
    uint32_t tile_hover;        /* app grid tile under the pointer */
    uint32_t tile_active;       /* pressed */
    uint32_t ink;               /* blended for "lighter/darker": white (dark) or black (light) */
    uint32_t light;             /* 1 in the light style */

    int headerbar_h;            /* 46 px, like libadwaita */
    int button_size;            /* square header-bar buttons */
    int radius;                 /* buttons */
    int spacing;                /* between elements */
    int margin;                 /* around content */
    int row_h;                  /* list rows */
    int topbar_h;               /* the desktop's top bar */
    int tile_w, tile_h;         /* one app in the grid: icon + name */
};

#define THEME_ACCENTS 6

const struct theme *theme_get(void);
int theme_is_light(void);

/* Choose the style ("dark"/"light") and accent (a name from theme_accent()).
 * Unknown names leave that part alone. */
void theme_set(int light, int accent);
int theme_accent_index(void);
int theme_find_accent(const char *name);           /* -1 if unknown */
int theme_find_style(const char *name);            /* 0 dark, 1 light, -1 unknown */

/* The i-th accent preset: its name and colour; NULL past the end. */
const char *theme_accent(int i, uint32_t *colour);

/* Apps: replace the whole theme with the kernel's copy. */
void theme_load(const struct theme *t);

#endif

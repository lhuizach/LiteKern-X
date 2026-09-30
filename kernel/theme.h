/* LiteKern X — the GUI theme: Adwaita (GNOME/Fedora), dark style
 * (decided 2026-09-30, docs/NON-GOALS.md).
 *
 * Colours are libadwaita's dark palette, with its translucent colours
 * pre-blended over the surface they sit on (the renderer has no per-widget
 * alpha yet). No widget or app hard-codes a colour: everything reads
 * theme_get(). The token names are the ones Phase 3 §5 (Customisation)
 * plans, so the light style and accent colours there extend this. */
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
    uint32_t accent;            /* accent-coloured text/icons on dark backgrounds */
    uint32_t border;            /* separators */
    uint32_t button_bg;         /* resting button */
    uint32_t button_hover;
    uint32_t button_active;     /* pressed */
    uint32_t row_hover;
    uint32_t row_selected;
    uint32_t warning;
    uint32_t destructive_bg;
    uint32_t destructive;       /* destructive text/icons */
    uint32_t desktop_bg;        /* the desktop, behind the app grid */
    uint32_t topbar_bg;
    uint32_t tile_hover;        /* app grid tile under the pointer */
    uint32_t tile_active;       /* pressed */

    int headerbar_h;            /* 46 px, like libadwaita */
    int button_size;            /* square header-bar buttons */
    int radius;                 /* buttons */
    int spacing;                /* between elements */
    int margin;                 /* around content */
    int row_h;                  /* list rows */
    int topbar_h;               /* the desktop's top bar */
    int tile_w, tile_h;         /* one app in the grid: icon + name */
};

const struct theme *theme_get(void);

#endif

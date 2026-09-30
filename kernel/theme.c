#include "kernel/theme.h"

/* libadwaita 1.x, dark. Translucent tokens (white at N%) are pre-blended
 * over the colour they're drawn on, noted per line. */
static const struct theme adwaita_dark = {
    .window_bg        = 0x222226,   /* @window_bg_color */
    .view_bg          = 0x1d1d20,   /* @view_bg_color */
    .headerbar_bg     = 0x2e2e32,   /* @headerbar_bg_color */
    .headerbar_border = 0x19191c,   /* @headerbar_shade_color over the header bar */
    .fg               = 0xffffff,   /* @window_fg_color */
    .fg_dim           = 0x9a9a9c,   /* fg at 55% over window_bg (.dim-label) */
    .accent_bg        = 0x3584e4,   /* @accent_bg_color (blue 3) */
    .accent_fg        = 0xffffff,
    .accent           = 0x78aeed,   /* @accent_color, dark */
    .border           = 0x3a3a3e,   /* white 12% over window_bg */
    .button_bg        = 0x444448,   /* white 10% over headerbar_bg */
    .button_hover     = 0x4e4e52,   /* white 15% */
    .button_active    = 0x5d5d61,   /* white 23% */
    .row_hover        = 0x2b2b2e,   /* white 5% over view_bg */
    .row_selected     = 0x243a57,   /* accent 25% over view_bg */
    .warning          = 0xcd9309,   /* @warning_color, dark */
    .destructive_bg   = 0xc01c28,   /* @destructive_bg_color */
    .destructive      = 0xff7b63,   /* @destructive_color, dark */
    .dialog_bg        = 0x36363a,   /* @dialog_bg_color */
    .desktop_bg       = 0x202634,   /* a plain dark slate until wallpapers (Phase 3 §5) */
    .topbar_bg        = 0x000000,   /* GNOME Shell's top bar */
    .tile_hover       = 0x363c48,   /* white 10% over desktop_bg (app grid) */
    .tile_active      = 0x414652,   /* white 15% */

    .headerbar_h = 46,
    .button_size = 34,
    .radius = 6,
    .spacing = 6,
    .margin = 12,
    .row_h = 40,
    .topbar_h = 30,
    .tile_w = 112,
    .tile_h = 104,
};

const struct theme *theme_get(void)
{
    return &adwaita_dark;
}

#include "kernel/theme.h"
#include "kernel/gfx.h"

/* libadwaita 1.x. Translucent tokens (white or black at N%) are pre-blended
 * over the colour they're drawn on, noted per line. */
static const struct theme adwaita_dark = {
    .window_bg        = 0x222226,   /* @window_bg_color */
    .view_bg          = 0x1d1d20,   /* @view_bg_color */
    .headerbar_bg     = 0x2e2e32,   /* @headerbar_bg_color */
    .headerbar_border = 0x19191c,   /* @headerbar_shade_color over the header bar */
    .fg               = 0xffffff,   /* @window_fg_color */
    .fg_dim           = 0x9a9a9c,   /* fg at 55% over window_bg (.dim-label) */
    .accent_fg        = 0xffffff,
    .border           = 0x3a3a3e,   /* white 12% over window_bg */
    .button_bg        = 0x444448,   /* white 10% over headerbar_bg */
    .button_hover     = 0x4e4e52,   /* white 15% */
    .button_active    = 0x5d5d61,   /* white 23% */
    .row_hover        = 0x2b2b2e,   /* white 5% over view_bg */
    .warning          = 0xcd9309,   /* @warning_color, dark */
    .destructive_bg   = 0xc01c28,   /* @destructive_bg_color */
    .destructive      = 0xff7b63,   /* @destructive_color, dark */
    .dialog_bg        = 0x36363a,   /* @dialog_bg_color */
    .desktop_bg       = 0x202634,   /* without a wallpaper: a dark slate */
    .topbar_bg        = 0x000000,   /* GNOME Shell's top bar */
    .tile_hover       = 0x363c48,   /* white 10% over desktop_bg */
    .tile_active      = 0x414652,   /* white 15% */
    .ink              = 0xffffff,
    .light            = 0,
};

static const struct theme adwaita_light = {
    .window_bg        = 0xfafafb,   /* @window_bg_color */
    .view_bg          = 0xffffff,   /* @view_bg_color */
    .headerbar_bg     = 0xebebed,   /* @headerbar_bg_color */
    .headerbar_border = 0xd6d6d9,   /* @headerbar_shade_color over the header bar */
    .fg               = 0x2e2e33,   /* @window_fg_color: black 80% over window_bg */
    .fg_dim           = 0x87878b,   /* fg at 55% */
    .accent_fg        = 0xffffff,
    .border           = 0xdcdcdf,   /* black 12% over window_bg */
    .button_bg        = 0xd4d4d6,   /* black 10% over headerbar_bg */
    .button_hover     = 0xc9c9cb,   /* black 15% */
    .button_active    = 0xb5b5b7,   /* black 23% */
    .row_hover        = 0xf2f2f2,   /* black 5% over view_bg */
    .warning          = 0x9c6e03,   /* @warning_color, light */
    .destructive_bg   = 0xe01b24,   /* @destructive_bg_color */
    .destructive      = 0xc01c28,   /* @destructive_color, light */
    .dialog_bg        = 0xffffff,   /* @dialog_bg_color */
    .desktop_bg       = 0xc8ccd6,   /* without a wallpaper: a pale slate */
    .topbar_bg        = 0x000000,   /* GNOME Shell's top bar stays black */
    .tile_hover       = 0xd6d9e1,
    .tile_active      = 0xe1e3ea,
    .ink              = 0x000000,
    .light            = 1,
};

/* GNOME 47's accent colours (the ones that read well on both styles). */
static const struct {
    const char *name;
    uint32_t bg;
} accents[THEME_ACCENTS] = {
    { "blue", 0x3584e4 }, { "teal", 0x2190a4 }, { "green", 0x3a944a },
    { "orange", 0xed5b00 }, { "red", 0xe62d42 }, { "purple", 0x9141ac },
};

static struct theme current;
static int built, light, accent;

static void build(void)
{
    current = light ? adwaita_light : adwaita_dark;
    current.accent_bg = accents[accent].bg;
    /* The accent as text/icons: lighter on dark, darker on light, as
     * libadwaita derives @accent_color. */
    current.accent = light ? gfx_mix(0x000000, accents[accent].bg, 40)
                           : gfx_mix(0xffffff, accents[accent].bg, 90);
    current.row_selected = gfx_mix(accents[accent].bg, current.view_bg, light ? 46 : 64);
    current.headerbar_h = 46;
    current.button_size = 34;
    current.radius = 6;
    current.spacing = 6;
    current.margin = 12;
    current.row_h = 40;
    current.topbar_h = 30;
    current.tile_w = 112;
    current.tile_h = 104;
    built = 1;
}

const struct theme *theme_get(void)
{
    if (!built)
        build();
    return &current;
}

int theme_is_light(void)
{
    return theme_get()->light != 0;
}

void theme_set(int new_light, int new_accent)
{
    if (new_light == 0 || new_light == 1)
        light = new_light;
    if (new_accent >= 0 && new_accent < THEME_ACCENTS)
        accent = new_accent;
    build();
}

int theme_accent_index(void)
{
    return accent;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

int theme_find_accent(const char *name)
{
    for (int i = 0; i < THEME_ACCENTS; i++)
        if (same(accents[i].name, name))
            return i;
    return -1;
}

int theme_find_style(const char *name)
{
    return same(name, "dark") ? 0 : same(name, "light") ? 1 : -1;
}

const char *theme_accent(int i, uint32_t *colour)
{
    if (i < 0 || i >= THEME_ACCENTS)
        return 0;
    if (colour)
        *colour = accents[i].bg;
    return accents[i].name;
}

void theme_load(const struct theme *t)
{
    current = *t;
    light = t->light != 0;
    built = 1;
}

#include "kernel/desktop.h"
#include "kernel/apps.h"
#include "kernel/console.h"
#include "kernel/font.h"
#include "kernel/icon.h"
#include "kernel/input.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/theme.h"
#include "kernel/wm.h"

#define GRID_TOP 48         /* gap between the top bar and the grid */
#define TILE_RADIUS 12

static const struct app *running;           /* the app in the open window, if any */
static int hover = -1, pressed = -1;        /* tile index; -1 = none */
static uint8_t last_buttons;

/* Tiles fill rows left to right, each row centred. */
static struct gfx_rect tile(int i)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    int cols = (s->w - 2 * t->margin) / t->tile_w;
    if (cols < 1)
        cols = 1;
    int row = i / cols, in_row = builtin_app_count - row * cols;
    if (in_row > cols)
        in_row = cols;
    int x0 = (s->w - in_row * t->tile_w) / 2;
    return (struct gfx_rect){ x0 + (i % cols) * t->tile_w, t->topbar_h + GRID_TOP + row * t->tile_h,
                              t->tile_w, t->tile_h };
}

static int tile_at(int x, int y)
{
    for (int i = 0; i < builtin_app_count; i++) {
        struct gfx_rect r = tile(i);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return i;
    }
    return -1;
}

static void draw_tile(int i)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    const struct app *a = &builtin_apps[i];
    struct gfx_rect r = tile(i);

    gfx_fill_rect(s, r.x, r.y, r.w, r.h, t->desktop_bg);
    if (pressed == i || hover == i)
        gfx_fill_round_rect(s, r.x + 4, r.y, r.w - 8, r.h, TILE_RADIUS,
                            pressed == i ? t->tile_active : t->tile_hover);
    const struct icon *ic = icon_find(a->icon);
    int ix = r.x + (r.w - 48) / 2, iy = r.y + 14;
    if (ic) {
        gfx_blit_alpha(s, ix, iy, ic->px, ic->w, ic->h, ic->w);
    } else {                    /* no icon built in: its initial on an accent tile */
        char initial[2] = { a->name[0], '\0' };
        gfx_fill_round_rect(s, ix, iy, 48, 48, 10, t->accent_bg);
        gfx_text(s, ix + 20, iy + 16, initial, t->accent_fg, GFX_TRANSPARENT);
    }
    gfx_text(s, r.x + (r.w - gfx_text_width(a->name)) / 2, iy + 48 + 12, a->name, t->fg,
             GFX_TRANSPARENT);
    screen_damage(r.x, r.y, r.w, r.h);
}

static void draw_topbar(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    int y = (t->topbar_h - FONT_H) / 2;
    gfx_fill_rect(s, 0, 0, s->w, t->topbar_h, t->topbar_bg);
    gfx_text(s, t->margin, y, "LiteKern X", t->fg, GFX_TRANSPARENT);
    gfx_text(s, t->margin + 1, y, "LiteKern X", t->fg, GFX_TRANSPARENT);    /* faux bold */
}

void desktop_show(void)
{
    if (!screen_ready() || wm_is_open())
        return;
    struct gfx_surface *s = screen_surface();
    hover = pressed = -1;
    console_set_visible(0);     /* the log keeps recording, but no longer draws over us */
    gfx_fill_rect(s, 0, 0, s->w, s->h, theme_get()->desktop_bg);
    draw_topbar();
    for (int i = 0; i < builtin_app_count; i++)
        draw_tile(i);
    screen_damage_all();
    screen_present();
}

static void launch(const struct app *a)
{
    kprintf("desktop: open %s\n", a->name);
    running = a;
    wm_open(a->name);
    if (a->open)
        a->open();
}

void desktop_input_mouse(int x, int y, uint8_t buttons)
{
    if (wm_is_open())
        return;
    int down = (buttons & MOUSE_LEFT) && !(last_buttons & MOUSE_LEFT);
    int up = !(buttons & MOUSE_LEFT) && (last_buttons & MOUSE_LEFT);
    last_buttons = buttons;

    int over = tile_at(x, y);
    if (over != hover) {
        int old = hover;
        hover = over;
        if (old >= 0)
            draw_tile(old);
        if (hover >= 0)
            draw_tile(hover);
    }
    if (down && over >= 0) {
        pressed = over;
        draw_tile(pressed);
    } else if (up && pressed >= 0) {
        int was = pressed;
        pressed = -1;
        draw_tile(was);
        if (was == over) {      /* released on the same tile: open it */
            screen_present();
            launch(&builtin_apps[was]);
            last_buttons = 0;
            return;
        }
    }
    screen_present();
}

static void log_event(const struct wm_event *ev)
{
    switch (ev->type) {
    case WM_EVENT_HEADER:
        kprintf("wm: header button %d\n", ev->id);
        break;
    case WM_EVENT_CLICK:
        kprintf("wm: click (%d, %d)\n", ev->x, ev->y);
        break;
    case WM_EVENT_KEY:
        kprintf("wm: key 0x%03x\n", ev->key.key);
        break;
    case WM_EVENT_CLOSE:
        break;
    }
}

void desktop_handle_events(void)
{
    struct wm_event ev;
    while (wm_is_open() && wm_poll_event(&ev)) {
        if (ev.type == WM_EVENT_CLOSE) {
            kprintf("wm: close\n");
            if (running && running->close)
                running->close();
            running = 0;
            wm_close();
            desktop_show();
        } else if (running && running->event) {
            running->event(&ev);
        } else {
            log_event(&ev);
        }
    }
}

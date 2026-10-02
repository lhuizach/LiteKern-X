/* LiteKern X — the shell: top bar, dock, app menu, windows' animations, and
 * the compositor that puts it all on screen (see kernel/desktop.h). */
#include "kernel/desktop.h"
#include "kernel/text.h"
#include "kernel/apps.h"
#include "kernel/console.h"
#include "kernel/driver.h"
#include "kernel/icon.h"
#include "kernel/input.h"
#include "kernel/lkx.h"
#include "kernel/pmm.h"
#include "kernel/power.h"
#include "kernel/timing.h"
#include "kernel/printk.h"
#include "kernel/rtc.h"
#include "kernel/screen.h"
#include "kernel/string.h"
#include "kernel/theme.h"
#include "kernel/wallpaper.h"
#include "kernel/ramdisk.h"
#include "kernel/splash.h"
#include "kernel/cursor.h"
#include "defaults.h"
#include "kernel/wm.h"

#define GRID_TOP 48         /* gap between the top bar and the app menu */
#define TILE_RADIUS 12
#define BAR_BUTTON_W 40     /* top bar buttons */
#define BAR_BUTTON_H 24
#define DOCK_ICON 48
#define DOCK_CELL 60
#define SEP_CELL 17         /* the line between favourites and the rest */
#define DOCK_PAD 8
#define DOCK_GAP 8          /* from the bottom of the screen */
#define DOCK_H (DOCK_ICON + 2 * DOCK_PAD)
#define DOCK_HIDDEN (DOCK_H + DOCK_GAP + 10)    /* how far down the dock goes to hide */
#define REVEAL_EDGE 2       /* px from the bottom that bring a hidden dock up */
#define HIDE_DELAY_MS 600   /* the dock stays this long after the pointer leaves it */
#define MENU_W 180
#define MENU_ITEM_H 34
#define MENU_PAD 6
#define WHITE 0xffffff
/* The shell (top bar, its menus, the dock) stays dark in both styles, like GNOME Shell. */
#define SHELL_FG      0xffffff
#define SHELL_POPOVER 0x36363a
#define DOT_GREY      0x8e8e93
#define DOT_W 6             /* a dot under an open app ... */
#define PILL_W 20           /* ... and the focused one's pill */

/* Translucent white over the wallpaper, as alpha out of 255. */
#define A_DOCK      20      /* the dock: 8% */
#define A_HOVER     38      /* 15% */
#define A_PRESSED   64      /* 25% */
#define A_DIM       90      /* black over the wallpaper while the app menu is open */

static int started;         /* desktop_start() ran: until then, the windows get the pointer */
static int menu_open;
static int grid_open;       /* the app menu */
static struct rtc_time clock_shown;
static uint32_t last_tick = ~0u;
static device_t *rtc;

/* The app each window belongs to: app_win[i] is builtin_apps[i]'s. */
static struct wm_window *app_win[APPS_MAX];
static uint32_t app_seq[APPS_MAX], seq;     /* the order they were opened */

static const struct theme *T(void)
{
    return theme_get();
}

static struct gfx_surface *S(void)
{
    return screen_surface();
}

static int inside(struct gfx_rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int app_index(const struct app *a)
{
    return (int)(a - builtin_apps);
}

static struct wm_window *window_of(int i)
{
    return i >= 0 && i < builtin_app_count ? app_win[i] : 0;
}

/* --- the compositor ---------------------------------------------------------------- */

#define MAX_DAMAGE 16
static struct gfx_rect dmg[MAX_DAMAGE];
static int ndmg, flushing;

/* The view being composed: something at screen (x, y) goes to VS at
 * (VX(x), VY(y)). */
static struct gfx_view *V;
#define VS (&V->s)
#define VX(x) ((x) - V->ox)
#define VY(y) ((y) - V->oy)

static int touches(struct gfx_rect a, struct gfx_rect b)
{
    return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;
}

void shell_damage(struct gfx_rect r)
{
    if (!screen_ready())
        return;
    r = gfx_rect_intersect(r, (struct gfx_rect){ 0, 0, S()->w, S()->h });
    if (gfx_rect_empty(r))
        return;
    for (int i = 0; i < ndmg;) {
        if (touches(r, dmg[i])) {
            r = gfx_rect_union(r, dmg[i]);
            dmg[i] = dmg[--ndmg];
            i = 0;
        } else {
            i++;
        }
    }
    if (ndmg == MAX_DAMAGE) {
        for (int i = 0; i < ndmg; i++)
            r = gfx_rect_union(r, dmg[i]);
        ndmg = 0;
    }
    dmg[ndmg++] = r;
}

static void damage_all(void)
{
    if (screen_ready())
        shell_damage((struct gfx_rect){ 0, 0, S()->w, S()->h });
}

static void draw_grid(void);
static void draw_dock(void);
static void draw_topbar(void);
static void draw_menu(void);

/* Everything in r, bottom to top. */
static void compose(struct gfx_rect r)
{
    struct gfx_view v = { gfx_sub(S(), r), r.x, r.y };
    struct gfx_view *outer = V;
    V = &v;
    wallpaper_draw(VS, v.ox, v.oy, grid_open ? A_DIM : 0);
    if (grid_open)
        draw_grid();
    else
        wm_draw(VS, v.ox, v.oy);
    if (started) {
        draw_dock();
        draw_topbar();
        if (menu_open)
            draw_menu();
    }
    V = outer;
}

void shell_flush(void)
{
    if (flushing || !screen_ready())
        return;                 /* a log line while composing: the loop below gets it */
    flushing = 1;
    while (ndmg) {
        struct gfx_rect list[MAX_DAMAGE];
        int n = ndmg;
        memcpy(list, dmg, sizeof(list[0]) * (uint32_t)n);
        ndmg = 0;
        for (int i = 0; i < n; i++) {
            compose(list[i]);
            screen_damage(list[i].x, list[i].y, list[i].w, list[i].h);
        }
    }
    screen_present();
    flushing = 0;
}

/* --- springs: everything in the dock moves on one ------------------------------------
 *
 * A value chases its target with a little momentum, so it overshoots a bit
 * and settles: the "blobby" feel. Fixed point, 1/256 units, one step per
 * TICK_MS. */

/* Recordings only (make EXTRA_CFLAGS=-DANIM_SLOW=6): everything N times
 * slower, so screenshots can catch the frames in between. */
#ifndef ANIM_SLOW
#define ANIM_SLOW 1
#endif

#define TICK_MS (16 * ANIM_SLOW)
#define FX(v) ((v) * 256)

struct spring {
    int x, v, t;
};

static int spring_step(struct spring *s, int k, int d)
{
    if (s->x == s->t && !s->v)
        return 0;
    s->v += (s->t - s->x) * k / 256 - s->v * d / 256;
    s->x += s->v;
    int dx = s->t - s->x;
    if (dx > -24 && dx < 24 && s->v > -24 && s->v < 24) {
        s->x = s->t;
        s->v = 0;
    }
    return 1;
}

static void spring_snap(struct spring *s)
{
    s->x = s->t;
    s->v = 0;
}

/* --- the dock ----------------------------------------------------------------------- */

#define I_SEP  APPS_MAX         /* the line */
#define I_APPS (APPS_MAX + 1)   /* Show Apps */
#define NITEMS (APPS_MAX + 2)

struct item {
    int in;                     /* in the dock now */
    int drawn;                  /* on screen (in, or still shrinking away) */
    struct spring pos;          /* centre x */
    struct spring scale;        /* FX(1): full size */
    struct spring dot_w, dot_white, dot_alpha;
};

static struct item items[NITEMS];
static struct spring dock_w;    /* the dock's width */
static struct spring reveal;    /* how far down it is: 0 up, FX(DOCK_HIDDEN) hidden */
static int peek;                /* brought up by the bottom edge over a maximised window */
static uint32_t hide_at, last_dock_tick;
static int dock_moving;
static char dock_logged[96];

static int dock_y(void)         /* its top, where it is now */
{
    return S()->h - DOCK_GAP - DOCK_H + reveal.x / 256;
}

static int dock_base_y(void)    /* its top when it's up */
{
    return S()->h - DOCK_GAP - DOCK_H;
}

/* The band the dock and its labels can draw in. */
static struct gfx_rect dock_band(void)
{
    int top = dock_base_y() - text_height(TEXT_BODY) - 24;
    return (struct gfx_rect){ 0, top, S()->w, S()->h - top };
}

/* The dock hides while a maximised window is in front (unless the pointer
 * brought it up), and never while the app menu is open. */
static int hide_mode(void)
{
    struct wm_window *top = wm_top_visible();
    return started && !grid_open && top && wm_is_maximised(top);
}

static int dock_up(void)        /* up enough to use */
{
    return started && reveal.x < FX(DOCK_H / 2);
}

static int dot_state(int i)     /* 0 closed, 1 minimised, 2 on screen, 3 focused */
{
    struct wm_window *w = window_of(i);
    if (!w)
        return 0;
    if (wm_is_minimised(w))
        return 1;
    return w == wm_focused() ? 3 : 2;
}

/* Work out where everything in the dock should be, and let it move there. */
static void dock_layout(int instant)
{
    int order[NITEMS], n = 0, cells[NITEMS];
    for (int i = 0; i < builtin_app_count; i++)
        if (builtin_apps[i].pinned)
            order[n++] = i;
    int first_other = n;
    for (;;) {                  /* the others that are open, oldest first */
        int best = -1;
        for (int i = 0; i < builtin_app_count; i++) {
            int listed = 0;
            for (int k = first_other; k < n; k++)
                listed |= order[k] == i;
            if (!builtin_apps[i].pinned && app_win[i] && !listed &&
                (best < 0 || app_seq[i] < app_seq[best]))
                best = i;
        }
        if (best < 0)
            break;
        order[n++] = best;
    }
    if (n > first_other) {      /* the line goes before them */
        for (int k = n; k > first_other; k--)
            order[k] = order[k - 1];
        order[first_other] = I_SEP;
        n++;
    }
    order[n++] = I_APPS;

    int total = 2 * DOCK_PAD;
    for (int k = 0; k < n; k++) {
        cells[k] = order[k] == I_SEP ? SEP_CELL : DOCK_CELL;
        total += cells[k];
    }
    for (int i = 0; i < NITEMS; i++)
        items[i].in = 0;
    int x = (S()->w - total) / 2 + DOCK_PAD;
    for (int k = 0; k < n; k++) {
        struct item *it = &items[order[k]];
        it->in = 1;
        it->pos.t = FX(x + cells[k] / 2);
        if (!it->drawn) {       /* new: it pops up where it belongs */
            it->pos.x = it->pos.t;
            it->pos.v = 0;
            it->scale.x = 0;
        }
        it->drawn = 1;
        it->scale.t = FX(1);
        x += cells[k];
    }
    for (int i = 0; i < NITEMS; i++)
        if (!items[i].in)
            items[i].scale.t = 0;               /* leaving: it shrinks away */
    dock_w.t = FX(total);

    for (int i = 0; i < builtin_app_count; i++) {
        struct item *it = &items[i];
        int st = dot_state(i);
        it->dot_alpha.t = st ? FX(1) : 0;
        if (st) {
            it->dot_w.t = FX(st == 3 ? PILL_W : DOT_W);
            it->dot_white.t = st >= 2 ? FX(1) : 0;
            if (!it->dot_alpha.x)               /* appearing: start as a dot */
                it->dot_w.x = FX(DOT_W);
        }
    }
    reveal.t = hide_mode() && !peek ? FX(DOCK_HIDDEN) : 0;

    if (instant) {
        for (int i = 0; i < NITEMS; i++) {
            struct item *it = &items[i];
            spring_snap(&it->pos);
            spring_snap(&it->scale);
            spring_snap(&it->dot_w);
            spring_snap(&it->dot_white);
            spring_snap(&it->dot_alpha);
            it->drawn = it->in;
        }
        spring_snap(&dock_w);
        spring_snap(&reveal);
    }
    dock_moving = 1;
    shell_damage(dock_band());

    /* Log what's in it when that changes (the tests read it). */
    char line[96];
    int len = 0;
    for (int k = 0; k < n && len < 80; k++) {
        const char *name = order[k] == I_SEP ? "|" : order[k] == I_APPS ? "Show Apps"
                                                                      : builtin_apps[order[k]].name;
        if (k)
            line[len++] = ' ';
        for (int c = 0; name[c] && len < 90; c++)
            line[len++] = name[c];
    }
    line[len] = '\0';
    if (strcmp(line, dock_logged)) {
        memcpy(dock_logged, line, (uint32_t)len + 1);
        kprintf("dock: %s\n", line);
    }
}

/* One step of every spring; 1 while anything still moves. */
static int dock_step(void)
{
    int moving = 0;
    for (int i = 0; i < NITEMS; i++) {
        struct item *it = &items[i];
        if (!it->drawn)
            continue;
        moving |= spring_step(&it->pos, 50, 100);
        moving |= spring_step(&it->scale, 46, 80);
        moving |= spring_step(&it->dot_w, 50, 90);
        moving |= spring_step(&it->dot_white, 40, 100);
        moving |= spring_step(&it->dot_alpha, 40, 100);
        if (!it->in && it->scale.x <= 0 && it->scale.t == 0) {
            it->drawn = 0;
            moving = 1;
        }
    }
    moving |= spring_step(&dock_w, 50, 100);
    moving |= spring_step(&reveal, 44, 90);
    return moving;
}

static void dock_tick(void)
{
    uint32_t now = uptime_ms();
    if (hide_at && (int32_t)(now - hide_at) >= 0) {
        hide_at = 0;
        peek = 0;
        reveal.t = hide_mode() ? FX(DOCK_HIDDEN) : 0;
        dock_moving = 1;
    }
    if (!dock_moving) {
        last_dock_tick = now;
        return;
    }
    uint32_t steps = (now - last_dock_tick) / TICK_MS;
    if (!steps)
        return;
    if (steps > 4)
        steps = 4;              /* slow machine: skip ahead rather than crawl */
    last_dock_tick = now;
    int moving = 0;
    for (uint32_t s = 0; s < steps; s++)
        moving = dock_step();
    dock_moving = moving;
    shell_damage(dock_band());
    shell_flush();
}

/* An item's centre on screen, with the dock narrowing as it sinks (the
 * blob look when it rises). */
static int dock_squash(void)    /* 1024: full width */
{
    int f = reveal.x / (DOCK_HIDDEN > 0 ? DOCK_HIDDEN : 1);     /* 0..256 */
    if (f < 0)
        f = 0;
    return 1024 - f * 330 / 256;
}

static int item_x(int i)
{
    int c = S()->w / 2;
    return c + (items[i].pos.x / 256 - c) * dock_squash() / 1024;
}

static struct gfx_rect item_rect(int i)
{
    return (struct gfx_rect){ item_x(i) - DOCK_CELL / 2, dock_y() + 2, DOCK_CELL, DOCK_H - 4 };
}

static struct gfx_rect dock_rect(void)
{
    int w = dock_w.x / 256 * dock_squash() / 1024;
    return (struct gfx_rect){ (S()->w - w) / 2, dock_y(), w, DOCK_H };
}

/* Where an app's icon sits in the dock (where its window shrinks to), up. */
static struct gfx_rect dock_icon_rect(int i)
{
    int cx = items[i].pos.t / 256;
    return (struct gfx_rect){ cx - DOCK_ICON / 2, dock_base_y() + DOCK_PAD, DOCK_ICON, DOCK_ICON };
}

/* --- hit testing ---------------------------------------------------------------------- */

enum hit_kind { H_NONE, H_HOME, H_POWER, H_RESTART, H_SHUTDOWN, H_DOCK, H_APPS, H_DOCK_BG, H_TILE,
                H_BAR };

struct hit {
    enum hit_kind kind;
    int app;
};

static struct hit hover, pressed;
static uint8_t last_buttons;

static int same(struct hit a, struct hit b)
{
    return a.kind == b.kind && a.app == b.app;
}

static struct gfx_rect home_rect(void)
{
    return (struct gfx_rect){ T()->spacing, (T()->topbar_h - BAR_BUTTON_H) / 2, BAR_BUTTON_W, BAR_BUTTON_H };
}

static struct gfx_rect power_rect(void)
{
    return (struct gfx_rect){ S()->w - T()->spacing - BAR_BUTTON_W, (T()->topbar_h - BAR_BUTTON_H) / 2,
                              BAR_BUTTON_W, BAR_BUTTON_H };
}

static struct gfx_rect menu_rect(void)
{
    return (struct gfx_rect){ S()->w - T()->spacing - MENU_W, T()->topbar_h + 4, MENU_W,
                              2 * MENU_ITEM_H + 2 * MENU_PAD };
}

static struct gfx_rect menu_item_rect(int k)
{
    struct gfx_rect m = menu_rect();
    return (struct gfx_rect){ m.x + MENU_PAD, m.y + MENU_PAD + k * MENU_ITEM_H, MENU_W - 2 * MENU_PAD,
                              MENU_ITEM_H };
}

static struct gfx_rect tile_rect(int i)
{
    const struct theme *t = T();
    int cols = (S()->w - 2 * t->margin) / t->tile_w;
    if (cols < 1)
        cols = 1;
    int row = i / cols, in_row = builtin_app_count - row * cols;
    if (in_row > cols)
        in_row = cols;
    int x0 = (S()->w - in_row * t->tile_w) / 2;
    int rows = (builtin_app_count + cols - 1) / cols;
    int space = S()->h - t->topbar_h - (DOCK_H + DOCK_GAP);
    int y0 = t->topbar_h + (space - rows * t->tile_h) / 2;
    if (y0 < t->topbar_h + GRID_TOP)
        y0 = t->topbar_h + GRID_TOP;
    return (struct gfx_rect){ x0 + (i % cols) * t->tile_w, y0 + row * t->tile_h, t->tile_w, t->tile_h };
}

static struct gfx_rect tile_icon_rect(int i)
{
    struct gfx_rect r = tile_rect(i);
    return (struct gfx_rect){ r.x + (r.w - DOCK_ICON) / 2, r.y + 14, DOCK_ICON, DOCK_ICON };
}

static struct hit hit_at(int x, int y)
{
    if (menu_open) {
        for (int k = 0; k < 2; k++)
            if (inside(menu_item_rect(k), x, y))
                return (struct hit){ k ? H_SHUTDOWN : H_RESTART, 0 };
        if (inside(menu_rect(), x, y))
            return (struct hit){ H_BAR, 0 };
    }
    if (y < T()->topbar_h) {
        if (inside(home_rect(), x, y))
            return (struct hit){ H_HOME, 0 };
        if (inside(power_rect(), x, y))
            return (struct hit){ H_POWER, 0 };
        return (struct hit){ H_BAR, 0 };
    }
    if (dock_up() && inside(dock_rect(), x, y)) {
        for (int i = 0; i < NITEMS; i++)
            if (i != I_SEP && items[i].in && items[i].scale.x >= FX(1) / 2 && inside(item_rect(i), x, y))
                return (struct hit){ i == I_APPS ? H_APPS : H_DOCK, i };
        return (struct hit){ H_DOCK_BG, 0 };
    }
    if (grid_open)
        for (int i = 0; i < builtin_app_count; i++)
            if (inside(tile_rect(i), x, y))
                return (struct hit){ H_TILE, i };
    return (struct hit){ H_NONE, 0 };
}

static void damage_hit(struct hit h)
{
    switch (h.kind) {
    case H_HOME:     shell_damage(home_rect()); break;
    case H_POWER:    shell_damage(power_rect()); break;
    case H_RESTART:
    case H_SHUTDOWN: shell_damage(menu_rect()); break;
    case H_DOCK:
    case H_APPS:     shell_damage(dock_band()); break;
    case H_TILE:     shell_damage(tile_rect(h.app)); break;
    default:         break;
    }
}

/* --- drawing (into the view being composed) --------------------------------------------- */

static int visible(struct gfx_rect r)
{
    return !gfx_rect_empty(gfx_rect_intersect(r, (struct gfx_rect){ V->ox, V->oy, V->s.w, V->s.h }));
}

static void draw_icon(const struct app *a, struct gfx_rect r, uint32_t alpha)
{
    const struct theme *t = T();
    const struct icon *ic = icon_find(a->icon);
    if (ic) {
        gfx_blit_alpha_scaled(VS, (struct gfx_rect){ VX(r.x), VY(r.y), r.w, r.h }, ic->px, ic->w, ic->h,
                              ic->w, alpha);
    } else {                    /* no icon built in: its initial on an accent tile */
        char initial[2] = { a->name[0], '\0' };
        gfx_blend_round_rect(VS, VX(r.x), VY(r.y), r.w, r.h, r.w * 10 / 48, t->accent_bg, alpha);
        if (r.w >= 40)
            text_draw(VS, VX(r.x) + (r.w - text_width(initial, TEXT_HEADING)) / 2,
                      VY(r.y) + (r.h - text_height(TEXT_HEADING)) / 2, initial, TEXT_HEADING, t->accent_fg);
    }
}

static void draw_home_icon(int cx, int cy, uint32_t c)
{
    for (int i = 0; i <= 7; i++)                    /* roof */
        gfx_fill_rect(VS, VX(cx - i), VY(cy - 7 + i), 2 * i + 1, 1, c);
    gfx_fill_rect(VS, VX(cx - 5), VY(cy + 1), 11, 6, c);      /* walls */
    gfx_fill_rect(VS, VX(cx - 1), VY(cy + 3), 3, 4, T()->topbar_bg);   /* door */
}

static void draw_power_icon(int cx, int cy, uint32_t c, uint32_t bg)
{
    gfx_fill_circle(VS, VX(cx), VY(cy + 1), 7, c);
    gfx_fill_circle(VS, VX(cx), VY(cy + 1), 5, bg);
    gfx_fill_rect(VS, VX(cx - 3), VY(cy - 7), 7, 5, bg);      /* the gap at the top */
    gfx_fill_rect(VS, VX(cx - 1), VY(cy - 8), 2, 8, c);       /* the stroke */
}

static uint32_t hit_bg(struct hit h, uint32_t base)
{
    if (same(h, pressed) && same(h, hover))
        return gfx_mix(WHITE, base, A_PRESSED);
    if (same(h, hover))
        return gfx_mix(WHITE, base, A_HOVER);
    return base;
}

/* "Wed 30 Sep  14:05", GNOME's format (24-hour); empty without a clock. */
static void clock_text(char text[24])
{
    static const char *const days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct rtc_time c = clock_shown;
    int n = 0;
    if (!rtc || !c.month) {
        text[0] = '\0';
        return;
    }
    for (const char *p = days[c.weekday % 7]; *p; p++)
        text[n++] = *p;
    text[n++] = ' ';
    if (c.day >= 10)
        text[n++] = (char)('0' + c.day / 10);
    text[n++] = (char)('0' + c.day % 10);
    text[n++] = ' ';
    for (const char *p = months[(c.month - 1) % 12]; *p; p++)
        text[n++] = *p;
    text[n++] = ' ';
    text[n++] = ' ';
    text[n++] = (char)('0' + c.hour / 10);
    text[n++] = (char)('0' + c.hour % 10);
    text[n++] = ':';
    text[n++] = (char)('0' + c.minute / 10);
    text[n++] = (char)('0' + c.minute % 10);
    text[n] = '\0';
}

static void log_clock(void)
{
    char text[24];
    clock_text(text);
    kprintf("shell: clock %s\n", text);
}

static struct gfx_rect clock_rect(void)
{
    return (struct gfx_rect){ (S()->w - 220) / 2, 0, 220, T()->topbar_h };
}

/* The name of the app in front, after Home. */
static const char *focused_name(void)
{
    struct wm_window *w = wm_focused();
    const struct app *a = w ? wm_owner(w) : 0;
    return a ? a->name : w ? wm_title(w) : "";
}

static void draw_topbar(void)
{
    const struct theme *t = T();
    struct gfx_rect bar = { 0, 0, S()->w, t->topbar_h };
    if (!visible(bar))
        return;
    gfx_fill_rect(VS, VX(0), VY(0), bar.w, bar.h, t->topbar_bg);
    int ty = (t->topbar_h - text_height(TEXT_BOLD)) / 2;
    const char *name = focused_name();
    if (name[0])
        text_draw(VS, VX(t->spacing + BAR_BUTTON_W + t->spacing * 2), VY(ty), name, TEXT_BOLD, SHELL_FG);
    char text[24];
    clock_text(text);
    text_draw(VS, VX((S()->w - text_width(text, TEXT_BOLD)) / 2), VY(ty), text, TEXT_BOLD, SHELL_FG);
    struct gfx_rect r = home_rect();
    uint32_t bg = hit_bg((struct hit){ H_HOME, 0 }, t->topbar_bg);
    gfx_fill_round_rect(VS, VX(r.x), VY(r.y), r.w, r.h, r.h / 2, bg);
    draw_home_icon(r.x + r.w / 2, r.y + r.h / 2, SHELL_FG);
    r = power_rect();
    bg = menu_open ? gfx_mix(WHITE, t->topbar_bg, A_HOVER) : hit_bg((struct hit){ H_POWER, 0 }, t->topbar_bg);
    gfx_fill_round_rect(VS, VX(r.x), VY(r.y), r.w, r.h, r.h / 2, bg);
    draw_power_icon(r.x + r.w / 2, r.y + r.h / 2, SHELL_FG, bg);
}

static void draw_menu(void)
{
    struct gfx_rect m = menu_rect();
    if (!visible(m))
        return;
    gfx_fill_round_rect(VS, VX(m.x), VY(m.y), m.w, m.h, 12, SHELL_POPOVER);
    for (int k = 0; k < 2; k++) {
        struct gfx_rect r = menu_item_rect(k);
        int disabled = k == 1 && !power_can_off();  /* no ACPI found */
        uint32_t bg = disabled ? SHELL_POPOVER
                               : hit_bg((struct hit){ k ? H_SHUTDOWN : H_RESTART, 0 }, SHELL_POPOVER);
        if (bg != SHELL_POPOVER)
            gfx_fill_round_rect(VS, VX(r.x), VY(r.y), r.w, r.h, 6, bg);
        text_draw(VS, VX(r.x + 12), VY(r.y + (r.h - text_height(TEXT_BODY)) / 2),
                  k ? "Shut Down" : "Restart", TEXT_BODY,
                  disabled ? gfx_mix(SHELL_FG, SHELL_POPOVER, 110) : SHELL_FG);
    }
}

static void draw_grid(void)
{
    for (int i = 0; i < builtin_app_count; i++) {
        struct gfx_rect r = tile_rect(i);
        if (!visible(r))
            continue;
        struct hit h = { H_TILE, i };
        if (same(h, hover))
            gfx_blend_round_rect(VS, VX(r.x + 4), VY(r.y), r.w - 8, r.h, TILE_RADIUS, WHITE,
                                 same(h, pressed) ? A_PRESSED : A_HOVER);
        const struct app *a = &builtin_apps[i];
        draw_icon(a, tile_icon_rect(i), 255);
        text_draw(VS, VX(r.x + (r.w - text_width(a->name, TEXT_BODY)) / 2), VY(r.y + 14 + 48 + 10), a->name,
                  TEXT_BODY, SHELL_FG);
    }
}

static void draw_dock(void)
{
    if (reveal.x >= FX(DOCK_HIDDEN - 2) || !visible(dock_band()))
        return;
    struct gfx_rect d = dock_rect();
    int y = dock_y();
    gfx_blend_round_rect(VS, VX(d.x), VY(d.y), d.w, d.h, 18, WHITE, A_DOCK);
    for (int i = 0; i < NITEMS; i++) {
        struct item *it = &items[i];
        if (!it->drawn || it->scale.x <= 0)
            continue;
        int cx = item_x(i), sc = it->scale.x;       /* FX(1): full size */
        if (i == I_SEP) {
            int h = 36 * sc / 256;
            gfx_blend_round_rect(VS, VX(cx), VY(y + (DOCK_H - h) / 2), 1, h, 0, WHITE, 70);
            continue;
        }
        struct hit h = { i == I_APPS ? H_APPS : H_DOCK, i };
        int lit = same(h, pressed) && same(h, hover) ? A_PRESSED : same(h, hover) ? A_HOVER
                : i == I_APPS && grid_open ? A_HOVER : 0;
        if (lit && it->in)
            gfx_blend_round_rect(VS, VX(cx - DOCK_CELL / 2 + 2), VY(y + 2), DOCK_CELL - 4, DOCK_H - 4, 12,
                                 WHITE, (uint32_t)lit);
        int size = DOCK_ICON * sc / 256;
        struct gfx_rect ir = { cx - size / 2, y + DOCK_PAD + DOCK_ICON / 2 - size / 2, size, size };
        uint32_t alpha = sc >= FX(1) ? 255 : (uint32_t)(sc * 255 / FX(1));
        if (i == I_APPS) {                          /* nine dots, like GNOME's "Show Apps" */
            for (int k = 0; k < 9; k++)
                gfx_fill_circle(VS, VX(cx - 10 * sc / 256 + (k % 3) * 10 * sc / 256),
                                VY(y + DOCK_H / 2 - 10 * sc / 256 + (k / 3) * 10 * sc / 256),
                                3 * sc / 256 > 0 ? 3 * sc / 256 : 1, SHELL_FG);
            continue;
        }
        draw_icon(&builtin_apps[i], ir, alpha);
        /* The dot: grey (minimised), white (on screen), a pill (focused). */
        if (it->dot_alpha.x > 0) {
            int dw = it->dot_w.x / 256 < 4 ? 4 : it->dot_w.x / 256;
            int white = it->dot_white.x < 0 ? 0 : it->dot_white.x > FX(1) ? 255 : it->dot_white.x * 255 / FX(1);
            int a = it->dot_alpha.x > FX(1) ? 255 : it->dot_alpha.x * 255 / FX(1);
            gfx_blend_round_rect(VS, VX(cx - dw / 2), VY(y + DOCK_H - 6), dw, 4, 2,
                                 gfx_mix(WHITE, DOT_GREY, (uint32_t)white), (uint32_t)(a * sc / FX(1) > 255 ? 255 : a * sc / FX(1)));
        }
    }
    /* The hovered item's name above it (GNOME's tooltip). */
    if ((hover.kind == H_DOCK || hover.kind == H_APPS) && dock_up() && !pressed.kind) {
        const char *name = hover.kind == H_APPS ? "Show Apps" : builtin_apps[hover.app].name;
        int lh = text_height(TEXT_BODY), w = text_width(name, TEXT_BODY) + 24;
        int x = item_x(hover.app) - w / 2, ly = y - 8 - lh - 10;
        gfx_fill_round_rect(VS, VX(x), VY(ly), w, lh + 10, 8, SHELL_POPOVER);
        text_draw(VS, VX(x + 12), VY(ly + 5), name, TEXT_BODY, SHELL_FG);
    }
}

/* --- window animations (Phase 3 §2, redone for floating windows in §6) -------------------
 *
 * A window is drawn once into a snapshot, left out of the picture, and the
 * snapshot is drawn scaled and faded over a copy of the rest, frame by
 * frame. Each frame is timed: if one takes longer than ANIM_MAX_FRAME_MS
 * (a slow machine), the rest are skipped, so an animation never makes
 * things feel slower. The cost is logged for measuring on the EeePC. */

#define ANIM_MAX_FRAME_MS 30

enum easing { EASE_OUT, EASE_BACK, EASE_IN_OUT };

struct ghost {
    struct wm_window *w;
    struct gfx_surface snap;
    struct gfx_rect from, to;
    int a0, a1;             /* alpha, 0..255 */
    int r0, r1;             /* corner radius */
};

static uint32_t *anim_save;             /* the picture without the ghosts */
static uint32_t *snap_buf[WM_MAX];      /* one snapshot per ghost, made on first use */

/* t and the result are 0..1024 (EASE_BACK overshoots a little past 1024). */
static int ease(enum easing e, int t)
{
    int64_t u = t - 1024;
    switch (e) {
    case EASE_BACK:         /* 1 + c3 u^3 + c1 u^2, c1 = 1.2 (a gentle overshoot) */
        return (int)(1024 + 2253 * u * u / 1024 * u / 1024 / 1024 + 1229 * u * u / 1024 / 1024);
    case EASE_IN_OUT:       /* smoothstep */
        return (int)((int64_t)t * t / 1024 * (3072 - 2 * t) / 1024);
    case EASE_OUT:
    default:
        return (int)(1024 + u * u / 1024 * u / 1024);
    }
}

static struct gfx_rect lerp(struct gfx_rect a, struct gfx_rect b, int e)
{
    return (struct gfx_rect){ a.x + (b.x - a.x) * e / 1024, a.y + (b.y - a.y) * e / 1024,
                              a.w + (b.w - a.w) * e / 1024, a.h + (b.h - a.h) * e / 1024 };
}

/* The window as it looks now, into ghost slot k. 0 if no memory. */
static int snapshot(int k, struct wm_window *w, struct gfx_surface *out)
{
    uint32_t bytes = (uint32_t)(S()->w * S()->h * 4);
    if (!snap_buf[k])
        snap_buf[k] = (uint32_t *)pmm_alloc_contiguous((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!snap_buf[k])
        return 0;
    struct gfx_rect r = wm_rect(w);
    *out = (struct gfx_surface){ snap_buf[k], r.w, r.h, r.w };
    gfx_fill_rect(out, 0, 0, r.w, r.h, T()->window_bg);
    wm_draw_one(out, r.x, r.y, w, r);
    return 1;
}

/* Play the ghosts (their windows already left out of the picture). */
static void anim_run(struct ghost *g, int n, int frames, int ms, enum easing e, const char *what,
                     const char *name)
{
    struct gfx_surface *s = S();
    if (!anim_save || !n)
        return;
    ms *= ANIM_SLOW;
    int drawn = 0;
    shell_flush();                          /* the picture as it is without them */
    compose((struct gfx_rect){ 0, 0, s->w, s->h });
    struct gfx_surface save = { anim_save, s->w, s->h, s->w };
    gfx_blit(&save, 0, 0, s, 0, 0, s->w, s->h);

    struct gfx_rect prev[WM_MAX];
    uint32_t t0 = uptime_ms(), worst = 0;
    int done = 0;
    for (int f = 1; f <= frames; f++) {
        uint32_t f0 = uptime_ms();
        int t = f * 1024 / frames, et = ease(e, t), ea = ease(EASE_OUT, t);
        for (int k = 0; k < n && done; k++) {
            gfx_blit(s, prev[k].x, prev[k].y, &save, prev[k].x, prev[k].y, prev[k].w, prev[k].h);
            screen_damage(prev[k].x, prev[k].y, prev[k].w, prev[k].h);
        }
        for (int k = 0; k < n; k++) {
            struct gfx_rect r = lerp(g[k].from, g[k].to, et);
            int alpha = g[k].a0 + (g[k].a1 - g[k].a0) * ea / 1024;
            int rad = g[k].r0 + (g[k].r1 - g[k].r0) * t / 1024;
            gfx_blit_scaled(s, r, &g[k].snap, (struct gfx_rect){ 0, 0, g[k].snap.w, g[k].snap.h },
                            (uint32_t)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha), rad);
            screen_damage(r.x, r.y, r.w, r.h);
            prev[k] = r;
        }
        done = 1;
        drawn++;
        screen_present();
        uint32_t took = uptime_ms() - f0;
        if (took > worst)
            worst = took;
        if (took > ANIM_MAX_FRAME_MS && f < frames) {
            /* Too slow here: jump to the last frame. */
            f = frames - 1;
            continue;
        }
        while (uptime_ms() - t0 < (uint32_t)(f * ms / frames))
            __asm__ volatile("pause");
    }
    for (int k = 0; k < n && done; k++)
        shell_damage(prev[k]);              /* the final picture is composed by the caller */
    kprintf("anim: %s %s, %d frames, %u ms, slowest frame %u ms\n", what, name, drawn,
            uptime_ms() - t0, worst);
}

static const char *name_of(struct wm_window *w)
{
    const struct app *a = wm_owner(w);
    return a ? a->name : wm_title(w);
}

/* The app's icon in the dock, or the middle of the bottom edge. */
static struct gfx_rect icon_target(struct wm_window *w)
{
    const struct app *a = wm_owner(w);
    if (a && items[app_index(a)].in)
        return dock_icon_rect(app_index(a));
    return (struct gfx_rect){ S()->w / 2 - DOCK_ICON / 2, dock_base_y() + DOCK_PAD, DOCK_ICON, DOCK_ICON };
}

static void anim_open(struct wm_window *w, struct gfx_rect from)
{
    struct ghost g = { w, { 0 }, from, wm_rect(w), 0, 255, 8, WM_RADIUS };
    if (!snapshot(0, w, &g.snap))
        return;
    wm_set_hidden(w, 1);
    anim_run(&g, 1, 12, 220, EASE_BACK, "open", name_of(w));
    wm_set_hidden(w, 0);
}

static void anim_close(struct wm_window *w)
{
    struct gfx_rect r = wm_rect(w);
    struct ghost g = { w, { 0 }, r, { r.x + r.w / 14, r.y + r.h / 14, r.w - r.w / 7, r.h - r.h / 7 },
                       255, 0, wm_is_maximised(w) ? 0 : WM_RADIUS, WM_RADIUS };
    if (!snapshot(0, w, &g.snap))
        return;
    wm_set_hidden(w, 1);
    anim_run(&g, 1, 8, 150, EASE_OUT, "close", name_of(w));
}

/* Minimise every window in list into the dock, together. */
static void minimise_windows(struct wm_window **list, int n)
{
    struct ghost g[WM_MAX];
    int ng = 0;
    for (int k = 0; k < n; k++) {
        struct wm_window *w = list[k];
        struct ghost *gh = &g[ng];
        gh->w = w;
        gh->from = wm_rect(w);
        gh->a0 = 255;
        gh->a1 = 40;
        gh->r0 = wm_is_maximised(w) ? 0 : WM_RADIUS;
        gh->r1 = 10;
        int snapped = snapshot(ng, w, &gh->snap);
        kprintf("desktop: minimise %s\n", name_of(w));
        wm_set_minimised(w, 1);
        if (snapped)
            ng++;
    }
    dock_layout(0);
    for (int k = 0; k < ng; k++)
        g[k].to = icon_target(g[k].w);
    anim_run(g, ng, 12, 240, EASE_IN_OUT, "minimise", ng == 1 ? name_of(g[0].w) : "all");
    shell_flush();
}

static void restore_window(struct wm_window *w)
{
    struct ghost g = { w, { 0 }, icon_target(w), wm_rect(w), 40, 255, 10, wm_is_maximised(w) ? 0 : WM_RADIUS };
    int snapped = snapshot(0, w, &g.snap);
    kprintf("desktop: restore %s\n", name_of(w));
    wm_set_hidden(w, 1);
    wm_set_minimised(w, 0);             /* in front and focused */
    dock_layout(0);
    if (snapped)
        anim_run(&g, 1, 12, 240, EASE_BACK, "restore", name_of(w));
    wm_set_hidden(w, 0);
    shell_flush();
}

void shell_window_minimise(struct wm_window *w)
{
    minimise_windows(&w, 1);
}

void shell_window_maximise(struct wm_window *w, int maximise, struct gfx_rect from)
{
    struct ghost g = { w, { 0 }, from, from, 255, 255, wm_is_maximised(w) ? 0 : WM_RADIUS,
                       maximise ? 0 : WM_RADIUS };
    int snapped = snapshot(0, w, &g.snap);
    kprintf("desktop: %s %s\n", maximise ? "maximise" : "unmaximise", name_of(w));
    wm_set_hidden(w, 1);
    wm_set_maximised(w, maximise);
    g.to = wm_rect(w);
    dock_layout(0);                     /* the dock hides behind a maximised window */
    if (snapped)
        anim_run(&g, 1, 10, 180, EASE_OUT, maximise ? "maximise" : "unmaximise", name_of(w));
    wm_set_hidden(w, 0);
    shell_flush();
}

void shell_window_focus_changed(void)
{
    if (!started)
        return;
    shell_damage((struct gfx_rect){ 0, 0, S()->w, T()->topbar_h });     /* the app's name */
    dock_layout(0);                     /* the dots */
}

/* --- apps ------------------------------------------------------------------------------- */

static void set_grid(int open)
{
    if (grid_open == open)
        return;
    grid_open = open;
    kprintf("desktop: app menu %s\n", open ? "open" : "closed");
    if (open)
        wm_pointer_gone();
    dock_layout(0);
    damage_all();
}

static void menu_set(int open)
{
    if (menu_open == open)
        return;
    menu_open = open;
    shell_damage(menu_rect());
    shell_damage(power_rect());
}

static void launch(int i, struct gfx_rect from)
{
    const struct app *a = &builtin_apps[i];
    struct wm_window *w = app_win[i];
    if (w) {                            /* already open: bring it back */
        if (wm_is_minimised(w))
            restore_window(w);
        else
            wm_focus(w);
        return;
    }
    kprintf("desktop: open %s\n", a->name);
    w = wm_create(a->name, a->width > 0 ? a->width : 760, a->height > 0 ? a->height : 480, a->min_width,
                  a->min_height);
    if (!w) {
        kprintf("desktop: no room for another window\n");
        return;
    }
    wm_set_owner(w, a);
    app_win[i] = w;
    app_seq[i] = ++seq;
    dock_layout(0);
    anim_open(w, from);
    if (a->open)
        a->open(w);
    if (a->lkx && lkx_start(a, w) < 0) {    /* it couldn't start: nothing to show */
        app_win[i] = 0;
        wm_destroy(w);
        dock_layout(0);
    }
    shell_flush();
}

/* The window goes, fading out. */
static void close_window(struct wm_window *w)
{
    const struct app *a = wm_owner(w);
    if (a)
        kprintf("desktop: close %s\n", a->name);
    anim_close(w);
    if (a)
        app_win[app_index(a)] = 0;
    wm_destroy(w);
    dock_layout(0);
    shell_flush();
}

void desktop_app_ended(const struct app *a, struct wm_window *w)
{
    (void)a;
    close_window(w);
}

/* Home: every window into the dock (or the app menu closes). */
static void show_desktop(void)
{
    struct wm_window *list[WM_MAX];
    int n = 0;
    for (int i = 0; i < wm_count(); i++)
        if (!wm_is_minimised(wm_at(i)))
            list[n++] = wm_at(i);
    kprintf("desktop: show desktop\n");
    if (n)
        minimise_windows(list, n);
}

/* A dock icon: open it; or bring it back; or, if it's in front already,
 * minimise it (like Windows' taskbar). */
static void dock_click(int i)
{
    struct wm_window *w = app_win[i];
    if (!w)
        launch(i, dock_icon_rect(i));
    else if (wm_is_minimised(w))
        restore_window(w);
    else if (w == wm_focused())
        minimise_windows(&w, 1);
    else
        wm_focus(w);
}

/* Before restarting or switching off: the screen fades to black. */
static void fade_out(void)
{
    struct gfx_surface *s = S();
    menu_open = 0;
    damage_all();
    shell_flush();
    screen_set_overlay(0, 0, 0, 0, 0);     /* the pointer goes first */
    for (int i = 1; i <= 8; i++) {
        uint32_t t0 = uptime_ms();
        gfx_darken(s, 0, 0, s->w, s->h, (uint32_t)(i * 255 / 8 > 120 ? 120 : i * 255 / 8));
        screen_damage_all();
        screen_present();
        while (uptime_ms() - t0 < (uint32_t)(30 * ANIM_SLOW))
            __asm__ volatile("pause");
    }
    gfx_fill_rect(s, 0, 0, s->w, s->h, 0);
    screen_damage_all();
    screen_present();
}

static void activate(struct hit h)
{
    switch (h.kind) {
    case H_HOME:
        if (grid_open)
            set_grid(0);
        else
            show_desktop();
        break;
    case H_APPS:
        set_grid(!grid_open);
        break;
    case H_POWER:
        menu_set(!menu_open);
        break;
    case H_RESTART:
        kprintf("shell: restart\n");
        fade_out();
        power_restart();
    case H_SHUTDOWN:
        if (!power_can_off())
            break;
        kprintf("shell: shut down\n");
        fade_out();
        power_off();
    case H_TILE:
        set_grid(0);
        shell_flush();
        launch(h.app, tile_icon_rect(h.app));
        break;
    case H_DOCK:
        if (grid_open) {
            set_grid(0);
            shell_flush();
        }
        dock_click(h.app);
        break;
    default:
        break;
    }
}

/* --- starting, input, ticks ----------------------------------------------------------------- */

void desktop_start(void)
{
    if (screen_ready()) {       /* the animations' copy of the screen, made once */
        uint32_t bytes = (uint32_t)(S()->w * S()->h * 4);
        anim_save = (uint32_t *)pmm_alloc_contiguous((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    }
    ramdisk_init();             /* apps and wallpapers, from the boot disk */
    splash_progress(65);
    apps_init();
    rtc = device_find("rtc0");
    if (rtc && rtc->state != DEVICE_BOUND)
        rtc = 0;
    if (rtc) {
        dev_read(rtc, &clock_shown, sizeof(clock_shown));
        log_clock();
    }
    if (!screen_ready())
        return;
    wallpaper_init(DEFAULT_WALLPAPER);
    splash_progress(90);
    if (splash_active()) {
        splash_end();
        cursor_init();          /* hidden during the splash */
    }
    console_set_visible(0);     /* the log keeps recording, but no longer draws over us */
    wm_set_area((struct gfx_rect){ 0, T()->topbar_h, S()->w, S()->h - T()->topbar_h });
    started = 1;
    dock_layout(1);
    /* Self-tests may have left windows: they belong to no app, but show. */
    damage_all();
    shell_flush();
    /* The boot budget's finish line (docs/BOOT-BUDGET.md: <= 5 s from stage 1). */
    kprintf("[boot] desktop t=%u\n", uptime_ms());
    kprintf("desktop: ready\n");     /* everything is up: the tests wait for this */
}

void desktop_refresh(void)
{
    if (!started)
        return;
    damage_all();
    shell_flush();
}

/* Over a maximised window the dock hides; the bottom edge brings it up,
 * and it stays while the pointer is on it (and a moment after). */
static void track_peek(int x, int y)
{
    int want = hide_mode();
    if (!want) {
        if (peek || hide_at) {
            peek = 0;
            hide_at = 0;
        }
    } else if (y >= S()->h - REVEAL_EDGE) {
        if (!peek)
            kprintf("dock: shown (bottom edge)\n");
        peek = 1;
        hide_at = 0;
    } else if (peek) {
        struct gfx_rect d = dock_rect();
        struct gfx_rect near = { d.x - 16, d.y - 40, d.w + 32, S()->h - d.y + 40 };
        if (inside(near, x, y))
            hide_at = 0;
        else if (!hide_at)
            hide_at = uptime_ms() + HIDE_DELAY_MS;
    }
    int t = want && !peek ? FX(DOCK_HIDDEN) : 0;
    if (reveal.t != t) {
        reveal.t = t;
        dock_moving = 1;
    }
}

void desktop_input_mouse(int x, int y, uint8_t buttons)
{
    if (!started) {             /* the self-tests: just the windows */
        wm_input_mouse(x, y, buttons);
        shell_flush();
        return;
    }
    int down = (buttons & MOUSE_LEFT) && !(last_buttons & MOUSE_LEFT);
    int up = !(buttons & MOUSE_LEFT) && (last_buttons & MOUSE_LEFT);
    last_buttons = buttons;
    track_peek(x, y);
    if (wm_grabbed() && !pressed.kind) {    /* a drag or a press in a window */
        wm_input_mouse(x, y, buttons);
        shell_flush();
        return;
    }

    struct hit h = hit_at(x, y);
    if (!same(h, hover)) {
        damage_hit(hover);
        hover = h;
        damage_hit(hover);
    }
    if (h.kind != H_NONE)
        wm_pointer_gone();      /* the shell has the pointer */

    if (down) {
        if (menu_open && h.kind != H_RESTART && h.kind != H_SHUTDOWN && h.kind != H_POWER &&
            h.kind != H_BAR) {
            menu_set(0);        /* a click outside closes the menu */
        } else if (h.kind == H_NONE) {
            if (grid_open)
                set_grid(0);    /* a click on empty space closes the app menu */
            else
                wm_input_mouse(x, y, buttons);
        } else if (h.kind != H_BAR && h.kind != H_DOCK_BG) {
            pressed = h;
            damage_hit(h);
        }
    } else if (up && pressed.kind) {
        struct hit was = pressed;
        pressed = (struct hit){ H_NONE, 0 };
        damage_hit(was);
        if (same(was, h)) {
            if (was.kind != H_POWER)
                menu_set(0);
            shell_flush();
            activate(was);
        }
    } else if (!pressed.kind && h.kind == H_NONE && !grid_open) {
        wm_input_mouse(x, y, buttons);      /* hover and the rest, for the windows */
    }
    shell_flush();
}

int desktop_input_key(const struct key_event *k)
{
    if (!k->pressed || k->key != KEY_ESC)
        return 0;
    if (menu_open)
        menu_set(0);
    else if (grid_open)
        set_grid(0);
    else
        return 0;
    shell_flush();
    return 1;
}

void desktop_tick(void)
{
    if (!started)
        return;
    dock_tick();
    uint32_t ticks;
    if (!rtc || dev_ioctl(rtc, RTC_GET_TICKS, &ticks) < 0 || ticks == last_tick)
        return;
    last_tick = ticks;
    struct rtc_time now;
    dev_read(rtc, &now, sizeof(now));
    if (now.minute == clock_shown.minute && now.hour == clock_shown.hour &&
        now.day == clock_shown.day && clock_shown.month)
        return;
    clock_shown = now;
    log_clock();
    shell_damage(clock_rect());
    shell_flush();
}

int desktop_busy(void)
{
    return dock_moving || hide_at;
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
    default:                    /* pointer moves are too many to log; clicks already are */
        break;
    }
}

void desktop_handle_events(void)
{
    struct wm_event ev;
again:
    for (int i = 0; i < wm_count(); i++) {
        struct wm_window *w = wm_at(i);
        const struct app *a = wm_owner(w);
        if (a && a->lkx)
            continue;           /* a ring 3 app takes its own (SYS_WAIT_EVENT) */
        while (wm_poll_event(w, &ev)) {
            if (ev.type == WM_EVENT_CLOSE) {
                kprintf("wm: close\n");
                if (a && a->close)
                    a->close(w);
                close_window(w);
                goto again;     /* the list changed */
            }
            if (a && a->event)
                a->event(w, &ev);
            else
                log_event(&ev);
        }
    }
}

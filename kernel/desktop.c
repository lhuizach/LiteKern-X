/* LiteKern X — the shell: top bar, desktop (app grid + dock), launching apps.
 *
 * The desktop is just the background and a dock (Fedora-style): the apps,
 * then "Show Apps", which opens the app menu, a full-screen grid of apps.
 *
 * Everything clickable is a "target" with a rectangle: the top bar's Home
 * and power buttons (always), the power menu's items (while it's open),
 * the dock's items (on the desktop) and the app menu's tiles (while it's
 * open). One piece of pointer
 * logic handles them all: hover highlights, and a press released over the
 * same target activates it. */
#include "kernel/desktop.h"
#include "kernel/text.h"
#include "kernel/apps.h"
#include "kernel/console.h"
#include "kernel/driver.h"
#include "kernel/font.h"
#include "kernel/icon.h"
#include "kernel/input.h"
#include "kernel/lkx.h"
#include "kernel/pmm.h"
#include "kernel/power.h"
#include "kernel/timing.h"
#include "kernel/printk.h"
#include "kernel/rtc.h"
#include "kernel/screen.h"
#include "kernel/theme.h"
#include "kernel/wallpaper.h"
#include "kernel/ramdisk.h"
#include "kernel/splash.h"
#include "kernel/cursor.h"
#include "defaults.h"
#include "kernel/wm.h"

#define GRID_TOP 48         /* gap between the top bar and the grid */
#define TILE_RADIUS 12
#define BAR_BUTTON_W 40     /* top bar buttons */
#define BAR_BUTTON_H 24
#define DOCK_ICON 48
#define DOCK_CELL 60
#define DOCK_PAD 8
#define DOCK_GAP 8          /* from the bottom of the screen */
#define MENU_W 180
#define MENU_ITEM_H 34
#define MENU_PAD 6
#define WHITE 0xffffff
/* The shell (top bar, its menus, the dock) stays dark in both styles, like GNOME Shell. */
#define SHELL_FG      0xffffff
#define SHELL_POPOVER 0x36363a

enum target_kind { T_HOME, T_POWER, T_RESTART, T_SHUTDOWN, T_TILE, T_DOCK, T_APPS };

struct target {
    enum target_kind kind;
    int app;                /* T_TILE / T_DOCK: index into builtin_apps */
    struct gfx_rect r;
};

#define MAX_TARGETS (4 + 2 * 16)
static struct target targets[MAX_TARGETS];
static int ntargets;
static int hover = -1, pressed = -1;        /* target index; -1 = none */
static uint8_t last_buttons;

static const struct app *running;           /* the app in the open window, if any */
static int started;         /* desktop_start() ran: until then, ignore the pointer */
static int menu_open;
static int grid_open;       /* the app menu */
static uint32_t menu_under[(MENU_W + 8) * (2 * MENU_ITEM_H + 2 * MENU_PAD + 8)];
static struct gfx_rect menu_r;
static struct rtc_time clock_shown;
static uint32_t last_tick = ~0u;
static device_t *rtc;

static const struct theme *T(void)
{
    return theme_get();
}

static struct gfx_surface *S(void)
{
    return screen_surface();
}

static int desktop_visible(void)
{
    return started && !wm_is_open();
}

/* Translucent white over the wallpaper, as alpha out of 255. */
#define A_DOCK      20      /* the dock: 8% */
#define A_HOVER     38      /* 15% */
#define A_PRESSED   64      /* 25% */
#define A_DIM       90      /* black over the wallpaper while the app menu is open */

/* The wallpaper, dimmed while the app menu is open (like GNOME's overview). */
static void background(int x, int y, int w, int h)
{
    wallpaper_draw(x, y, w, h, grid_open ? A_DIM : 0);
}

/* --- layout -------------------------------------------------------------------- */

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
    int space = S()->h - t->topbar_h - (DOCK_ICON + 2 * DOCK_PAD + DOCK_GAP);
    int y0 = t->topbar_h + (space - rows * t->tile_h) / 2;
    if (y0 < t->topbar_h + GRID_TOP)
        y0 = t->topbar_h + GRID_TOP;
    return (struct gfx_rect){ x0 + (i % cols) * t->tile_w, y0 + row * t->tile_h, t->tile_w,
                              t->tile_h };
}

static struct gfx_rect dock_rect(void)
{
    int w = (builtin_app_count + 1) * DOCK_CELL + 2 * DOCK_PAD, h = DOCK_ICON + 2 * DOCK_PAD;
    return (struct gfx_rect){ (S()->w - w) / 2, S()->h - DOCK_GAP - h, w, h };
}

static struct gfx_rect dock_item_rect(int i)
{
    struct gfx_rect d = dock_rect();
    return (struct gfx_rect){ d.x + DOCK_PAD + i * DOCK_CELL, d.y + 2, DOCK_CELL, d.h - 4 };
}

static void add_target(enum target_kind kind, int app, struct gfx_rect r)
{
    if (ntargets < MAX_TARGETS)
        targets[ntargets++] = (struct target){ kind, app, r };
}

/* Which targets exist right now. */
static void build_targets(void)
{
    const struct theme *t = T();
    int y = (t->topbar_h - BAR_BUTTON_H) / 2;
    ntargets = 0;
    add_target(T_HOME, 0, (struct gfx_rect){ t->spacing, y, BAR_BUTTON_W, BAR_BUTTON_H });
    add_target(T_POWER, 0, (struct gfx_rect){ S()->w - t->spacing - BAR_BUTTON_W, y, BAR_BUTTON_W,
                                              BAR_BUTTON_H });
    if (menu_open) {
        int iy = menu_r.y + MENU_PAD;
        add_target(T_RESTART, 0, (struct gfx_rect){ menu_r.x + MENU_PAD, iy, MENU_W - 2 * MENU_PAD,
                                                    MENU_ITEM_H });
        add_target(T_SHUTDOWN, 0, (struct gfx_rect){ menu_r.x + MENU_PAD, iy + MENU_ITEM_H,
                                                     MENU_W - 2 * MENU_PAD, MENU_ITEM_H });
    }
    if (desktop_visible()) {
        int n = builtin_app_count < 16 ? builtin_app_count : 16;
        for (int i = 0; i < n; i++)
            add_target(T_DOCK, i, dock_item_rect(i));
        add_target(T_APPS, 0, dock_item_rect(n));
        if (grid_open)
            for (int i = 0; i < n; i++)
                add_target(T_TILE, i, tile_rect(i));
    }
    hover = pressed = -1;
}

/* --- drawing ------------------------------------------------------------------- */

static void draw_home_icon(struct gfx_surface *s, int cx, int cy, uint32_t c)
{
    for (int i = 0; i <= 7; i++)                    /* roof */
        gfx_fill_rect(s, cx - i, cy - 7 + i, 2 * i + 1, 1, c);
    gfx_fill_rect(s, cx - 5, cy + 1, 11, 6, c);      /* walls */
    gfx_fill_rect(s, cx - 1, cy + 3, 3, 4, T()->topbar_bg);   /* door */
}

static void draw_power_icon(struct gfx_surface *s, int cx, int cy, uint32_t c, uint32_t bg)
{
    gfx_fill_circle(s, cx, cy + 1, 7, c);
    gfx_fill_circle(s, cx, cy + 1, 5, bg);
    gfx_fill_rect(s, cx - 3, cy - 7, 7, 5, bg);      /* the gap at the top */
    gfx_fill_rect(s, cx - 1, cy - 8, 2, 8, c);       /* the stroke */
}

static uint32_t target_bg(int i, uint32_t base)
{
    if (i == pressed)
        return gfx_mix(WHITE, base, 64);
    if (i == hover)
        return gfx_mix(WHITE, base, 38);
    return base;
}

static void draw_target(int i);
static void draw_icon(const struct app *a, int x, int y);

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
    } else {
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
}

static void log_clock(void)
{
    char text[24];
    clock_text(text);
    kprintf("shell: clock %s\n", text);
}

static void draw_clock(void)
{
    const struct theme *t = T();
    char text[24];
    clock_text(text);
    int w = 220, x = (S()->w - w) / 2, y = (t->topbar_h - text_height(TEXT_BOLD)) / 2;
    gfx_fill_rect(S(), x, 0, w, t->topbar_h, t->topbar_bg);
    text_draw(S(), (S()->w - text_width(text, TEXT_BOLD)) / 2, y, text, TEXT_BOLD, 0xffffff);
    screen_damage(x, 0, w, t->topbar_h);
}

static void draw_topbar(void)
{
    const struct theme *t = T();
    gfx_fill_rect(S(), 0, 0, S()->w, t->topbar_h, t->topbar_bg);
    if (running) {              /* the open app's name, after Home */
        int x = t->spacing + BAR_BUTTON_W + t->spacing * 2, y = (t->topbar_h - text_height(TEXT_BOLD)) / 2;
        text_draw(S(), x, y, running->name, TEXT_BOLD, 0xffffff);
    }
    for (int i = 0; i < ntargets; i++)
        if (targets[i].kind == T_HOME || targets[i].kind == T_POWER)
            draw_target(i);
    draw_clock();
    screen_damage(0, 0, S()->w, t->topbar_h);
}

static void draw_menu(void)
{
    gfx_fill_round_rect(S(), menu_r.x, menu_r.y, menu_r.w, menu_r.h, 12, SHELL_POPOVER);
    for (int i = 0; i < ntargets; i++)
        if (targets[i].kind == T_RESTART || targets[i].kind == T_SHUTDOWN)
            draw_target(i);
    screen_damage(menu_r.x, menu_r.y, menu_r.w, menu_r.h);
}

/* The whole dock, items included: it's translucent over the wallpaper, so an
 * item can't be redrawn on its own without redrawing what's under it. */
static void draw_dock(void)
{
    struct gfx_rect d = dock_rect();
    background(d.x, d.y, d.w, d.h);
    gfx_blend_round_rect(S(), d.x, d.y, d.w, d.h, 18, WHITE, A_DOCK);
    for (int i = 0; i < ntargets; i++) {
        struct target *g = &targets[i];
        if (g->kind != T_DOCK && g->kind != T_APPS)
            continue;
        struct gfx_rect r = g->r;
        int lit = i == pressed ? A_PRESSED : i == hover ? A_HOVER
                : g->kind == T_APPS && grid_open ? A_HOVER : 0;
        if (lit)
            gfx_blend_round_rect(S(), r.x + 2, r.y, r.w - 4, r.h, 12, WHITE, (uint32_t)lit);
        if (g->kind == T_APPS)          /* nine dots, like GNOME's "Show Apps" */
            for (int k = 0; k < 9; k++)
                gfx_fill_circle(S(), r.x + r.w / 2 - 10 + (k % 3) * 10,
                                r.y + r.h / 2 - 10 + (k / 3) * 10, 3, SHELL_FG);
        else
            draw_icon(&builtin_apps[g->app], r.x + (r.w - DOCK_ICON) / 2, r.y + (r.h - DOCK_ICON) / 2);
    }
    screen_damage(d.x, d.y, d.w, d.h);
}

/* The app's name in a small label above its dock item, while hovered. */
static void draw_dock_label(void)
{
    struct gfx_rect d = dock_rect();
    int lh = text_height(TEXT_BODY), band_y = d.y - 8 - lh - 10;
    background(0, band_y, S()->w, lh + 10);
    if (hover >= 0 && (targets[hover].kind == T_DOCK || targets[hover].kind == T_APPS)) {
        const char *name = targets[hover].kind == T_APPS ? "Show Apps"
                                                         : builtin_apps[targets[hover].app].name;
        struct gfx_rect r = targets[hover].r;
        int w = text_width(name, TEXT_BODY) + 24, x = r.x + (r.w - w) / 2;
        gfx_fill_round_rect(S(), x, band_y, w, lh + 10, 8, 0x36363a);     /* GNOME's tooltip */
        text_draw(S(), x + 12, band_y + 5, name, TEXT_BODY, 0xffffff);
    }
    screen_damage(0, band_y, S()->w, lh + 10);
}

static void draw_icon(const struct app *a, int x, int y)
{
    const struct theme *t = T();
    const struct icon *ic = icon_find(a->icon);
    if (ic) {
        gfx_blit_alpha(S(), x, y, ic->px, ic->w, ic->h, ic->w);
    } else {                    /* no icon built in: its initial on an accent tile */
        char initial[2] = { a->name[0], '\0' };
        gfx_fill_round_rect(S(), x, y, 48, 48, 10, t->accent_bg);
        text_draw(S(), x + (48 - text_width(initial, TEXT_HEADING)) / 2, y + (48 - text_height(TEXT_HEADING)) / 2,
                  initial, TEXT_HEADING, t->accent_fg);
    }
}

static void draw_target(int i)
{
    const struct theme *t = T();
    struct target *g = &targets[i];
    struct gfx_rect r = g->r;
    switch (g->kind) {
    case T_HOME:
    case T_POWER: {
        uint32_t bg = target_bg(i, t->topbar_bg);
        gfx_fill_rect(S(), r.x, r.y, r.w, r.h, t->topbar_bg);
        gfx_fill_round_rect(S(), r.x, r.y, r.w, r.h, r.h / 2, bg);
        if (g->kind == T_HOME)
            draw_home_icon(S(), r.x + r.w / 2, r.y + r.h / 2, SHELL_FG);
        else
            draw_power_icon(S(), r.x + r.w / 2, r.y + r.h / 2, SHELL_FG, bg);
        break;
    }
    case T_RESTART:
    case T_SHUTDOWN: {
        int disabled = g->kind == T_SHUTDOWN;       /* needs ACPI (Phase 5) */
        uint32_t bg = disabled ? SHELL_POPOVER : target_bg(i, SHELL_POPOVER);
        gfx_fill_rect(S(), r.x, r.y, r.w, r.h, SHELL_POPOVER);
        gfx_fill_round_rect(S(), r.x, r.y, r.w, r.h, 6, bg);
        text_draw(S(), r.x + 12, r.y + (r.h - text_height(TEXT_BODY)) / 2, disabled ? "Shut Down" : "Restart", TEXT_BODY,
                  disabled ? gfx_mix(SHELL_FG, SHELL_POPOVER, 110) : SHELL_FG);
        break;
    }
    case T_TILE: {
        const struct app *a = &builtin_apps[g->app];
        background(r.x, r.y, r.w, r.h);
        if (i == pressed || i == hover)
            gfx_blend_round_rect(S(), r.x + 4, r.y, r.w - 8, r.h, TILE_RADIUS, WHITE,
                                 i == pressed ? A_PRESSED : A_HOVER);
        int ix = r.x + (r.w - 48) / 2, iy = r.y + 14;
        draw_icon(a, ix, iy);
        text_draw(S(), r.x + (r.w - text_width(a->name, TEXT_BODY)) / 2, iy + 48 + 10, a->name, TEXT_BODY,
                  0xffffff);
        break;
    }
    case T_DOCK:
    case T_APPS:
        draw_dock();
        return;
    }
    screen_damage(r.x, r.y, r.w, r.h);
}

/* --- the power menu ------------------------------------------------------------ */

static void menu_set(int open)
{
    struct gfx_surface *s = S();
    if (open == menu_open)
        return;
    if (open) {
        const struct theme *t = T();
        menu_r = (struct gfx_rect){ s->w - t->spacing - MENU_W, t->topbar_h + 4, MENU_W,
                                    2 * MENU_ITEM_H + 2 * MENU_PAD };
        for (int y = 0; y < menu_r.h; y++)          /* keep what it covers */
            for (int x = 0; x < menu_r.w; x++)
                menu_under[y * MENU_W + x] = s->px[(menu_r.y + y) * s->stride + menu_r.x + x];
        menu_open = 1;
        build_targets();
        draw_topbar();          /* the targets were rebuilt: drop stale highlights */
        draw_menu();
    } else {
        for (int y = 0; y < menu_r.h; y++)
            for (int x = 0; x < menu_r.w; x++)
                s->px[(menu_r.y + y) * s->stride + menu_r.x + x] = menu_under[y * MENU_W + x];
        screen_damage(menu_r.x, menu_r.y, menu_r.w, menu_r.h);
        menu_open = 0;
        build_targets();
        draw_topbar();
    }
}

/* --- apps ---------------------------------------------------------------------- */

static void draw_desktop(void)
{
    const struct theme *t = T();
    background(0, t->topbar_h, S()->w, S()->h - t->topbar_h);
    for (int i = 0; i < ntargets; i++)
        if (targets[i].kind == T_TILE)
            draw_target(i);
    draw_dock();
}

/* The whole desktop into the back buffer (not shown yet). */
static void render_desktop(void)
{
    started = 1;
    console_set_visible(0);     /* the log keeps recording, but no longer draws over us */
    menu_open = 0;
    grid_open = 0;
    build_targets();
    draw_topbar();
    draw_desktop();
    screen_damage_all();
}

void desktop_show(void)
{
    if (!screen_ready() || wm_is_open())
        return;
    render_desktop();
    screen_present();
}

/* --- open and close animations (Phase 3 §2) ----------------------------------------
 *
 * Opening an app grows its window out of the icon that was clicked; closing
 * shrinks it back into its dock icon, GNOME-like and short (ANIM_MS). Each
 * frame is timed: if one takes longer than ANIM_MAX_FRAME_MS (a slow
 * machine), the rest are skipped, so an animation never makes things feel
 * slower. The cost is logged for measuring on the EeePC. */

#define ANIM_FRAMES 8
#define ANIM_MS 140
#define ANIM_MAX_FRAME_MS 30

static uint32_t *anim_save;     /* a copy of the desktop, for closing */

/* Ease-out: fast start, gentle stop. t and the result are 0..1024. */
static int ease(int t)
{
    int u = 1024 - t;
    return 1024 - (int)((int64_t)u * u / 1024 * u / 1024);
}

static struct gfx_rect lerp(struct gfx_rect a, struct gfx_rect b, int t)
{
    int e = ease(t);
    return (struct gfx_rect){ a.x + (b.x - a.x) * e / 1024, a.y + (b.y - a.y) * e / 1024,
                              a.w + (b.w - a.w) * e / 1024, a.h + (b.h - a.h) * e / 1024 };
}

/* The window as it grows or shrinks: its background and header bar. */
static void draw_window_frame(struct gfx_rect r, int t)
{
    const struct theme *th = T();
    int radius = 16 - 16 * ease(t) / 1024;
    gfx_fill_round_rect(S(), r.x, r.y, r.w, r.h, radius, th->window_bg);
    int head = th->headerbar_h * r.h / (S()->h - th->topbar_h);
    if (head > 2) {
        gfx_fill_round_rect(S(), r.x, r.y, r.w, head, radius, th->headerbar_bg);
        gfx_fill_rect(S(), r.x, r.y + head - (head > radius ? head - radius : 0), r.w,
                      head > radius ? head - radius : 0, th->headerbar_bg);
    }
}

static struct gfx_rect window_rect(void)
{
    return (struct gfx_rect){ 0, T()->topbar_h, S()->w, S()->h - T()->topbar_h };
}

/* Wait until `ms` after t0; 0 if the frame just drawn was too slow. */
static int frame_wait(uint32_t t0, uint32_t frame_start, int ms)
{
    if (uptime_ms() - frame_start > ANIM_MAX_FRAME_MS)
        return 0;
    while (uptime_ms() - t0 < (uint32_t)ms)
        __asm__ volatile("pause");
    return 1;
}

static void anim_open(struct gfx_rect from, const char *name)
{
    uint32_t t0 = uptime_ms(), worst = 0;
    int frames = 0;
    struct gfx_rect to = window_rect();
    for (int i = 1; i <= ANIM_FRAMES; i++) {
        uint32_t f0 = uptime_ms();
        struct gfx_rect r = lerp(from, to, i * 1024 / ANIM_FRAMES);
        draw_window_frame(r, i * 1024 / ANIM_FRAMES);
        screen_damage(r.x, r.y, r.w, r.h);
        screen_present();
        frames++;
        if (uptime_ms() - f0 > worst)
            worst = uptime_ms() - f0;
        if (!frame_wait(t0, f0, i * ANIM_MS / ANIM_FRAMES))
            break;
    }
    kprintf("anim: open %s, %d frames, %u ms, slowest frame %u ms\n", name, frames,
            uptime_ms() - t0, worst);
}

/* The desktop is drawn (not shown) when this starts. */
static void anim_close(struct gfx_rect to, const char *name)
{
    struct gfx_surface *s = S();
    if (!anim_save)
        return;                 /* no memory for it: no close animation */
    struct gfx_surface save = { anim_save, s->w, s->h, s->w };
    gfx_blit(&save, 0, 0, s, 0, 0, s->w, s->h);
    uint32_t t0 = uptime_ms(), worst = 0;
    int frames = 0;
    struct gfx_rect from = window_rect(), prev = from;
    for (int i = 0; i < ANIM_FRAMES; i++) {
        uint32_t f0 = uptime_ms();
        struct gfx_rect r = lerp(from, to, i * 1024 / ANIM_FRAMES);
        gfx_blit(s, prev.x, prev.y, &save, prev.x, prev.y, prev.w, prev.h);
        draw_window_frame(r, 1024 - i * 1024 / ANIM_FRAMES);
        screen_damage(prev.x, prev.y, prev.w, prev.h);
        screen_present();
        prev = r;
        frames++;
        if (uptime_ms() - f0 > worst)
            worst = uptime_ms() - f0;
        if (!frame_wait(t0, f0, (i + 1) * ANIM_MS / ANIM_FRAMES))
            break;
    }
    gfx_blit(s, prev.x, prev.y, &save, prev.x, prev.y, prev.w, prev.h);
    screen_damage_all();
    kprintf("anim: close %s, %d frames, %u ms, slowest frame %u ms\n", name, frames,
            uptime_ms() - t0, worst);
}

/* Where an app's icon is in the dock (where its window shrinks to). */
static struct gfx_rect app_icon_rect(const struct app *a)
{
    int i = (int)(a - builtin_apps);
    struct gfx_rect r = dock_item_rect(i);
    return (struct gfx_rect){ r.x + (r.w - DOCK_ICON) / 2, r.y + (r.h - DOCK_ICON) / 2, DOCK_ICON,
                              DOCK_ICON };
}

/* After an app: the desktop comes back, the window shrinking into its icon. */
static void finish_app(const struct app *a)
{
    kprintf("desktop: close %s\n", a->name);
    running = 0;
    wm_close();
    if (!screen_ready())
        return;
    render_desktop();
    anim_close(app_icon_rect(a), a->name);
    screen_present();
}

void desktop_start(void)
{
    /* The build-time defaults (make STYLE=... ACCENT=... WALLPAPER=...). */
    if (screen_ready()) {       /* the close animation's copy of the desktop, made once */
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
    started = 1;
    if (wm_is_open()) {         /* a self-test left a window open: just the top bar */
        build_targets();
        draw_topbar();
        screen_present();
    } else {
        desktop_show();
    }
    kprintf("desktop: ready\n");     /* everything is up: the tests wait for this */
}

void desktop_refresh(void)
{
    if (!started)
        return;
    if (desktop_visible()) {
        desktop_show();
        return;
    }
    build_targets();
    draw_topbar();
    screen_present();
}

static void launch(const struct app *a, struct gfx_rect from)
{
    kprintf("desktop: open %s\n", a->name);
    menu_open = 0;
    anim_open(from, a->name);
    grid_open = 0;
    running = a;
    wm_open(a->name);
    build_targets();
    draw_topbar();
    screen_present();
    if (a->open)
        a->open();
    if (a->lkx) {               /* a ring 3 app: it runs, here, until it exits */
        lkx_run(a);
        finish_app(a);
    }
}

/* Close the open window (and its app), back to the desktop. */
static void close_app(void)
{
    if (running && running->lkx) {
        wm_request_close();     /* the app closes itself (or is ended at its next wait) */
        return;
    }
    if (!running) {             /* a window no app owns (the self-tests open one) */
        wm_close();
        desktop_show();
        return;
    }
    const struct app *a = running;
    if (a->close)
        a->close();
    finish_app(a);
}

/* Open or close the app menu. */
static void set_grid(int open)
{
    grid_open = open;
    kprintf("desktop: app menu %s\n", open ? "open" : "closed");
    build_targets();
    draw_desktop();
    screen_damage_all();
}

static void activate(const struct target *g)
{
    switch (g->kind) {
    case T_HOME:
        if (wm_is_open())
            close_app();
        else if (grid_open)
            set_grid(0);
        break;
    case T_APPS:
        set_grid(!grid_open);
        break;
    case T_POWER:
        menu_set(!menu_open);
        break;
    case T_RESTART:
        kprintf("shell: restart\n");
        power_restart();
    case T_SHUTDOWN:
        break;
    case T_TILE:
    case T_DOCK:
        launch(&builtin_apps[g->app], g->r);
        break;
    }
}

/* --- input --------------------------------------------------------------------- */

static int target_at(int x, int y)
{
    for (int i = 0; i < ntargets; i++) {
        struct gfx_rect r = targets[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return i;
    }
    return -1;
}

int desktop_input_mouse(int x, int y, uint8_t buttons)
{
    if (!started)
        return 0;
    int down = (buttons & MOUSE_LEFT) && !(last_buttons & MOUSE_LEFT);
    int up = !(buttons & MOUSE_LEFT) && (last_buttons & MOUSE_LEFT);
    last_buttons = buttons;
    int over = target_at(x, y), consumed = desktop_visible() || menu_open;

    if (over != hover) {
        int old = hover;
        hover = over;
        if (old >= 0)
            draw_target(old);
        if (hover >= 0)
            draw_target(hover);
        if (desktop_visible())
            draw_dock_label();
    }
    if (down) {
        if (menu_open && over < 0) {
            menu_set(0);                /* a click outside closes the menu */
            consumed = 1;
        } else if (over >= 0) {
            pressed = over;
            draw_target(over);
            consumed = 1;
        } else if (y < T()->topbar_h) {
            consumed = 1;               /* the top bar isn't the app's */
        } else if (grid_open) {
            set_grid(0);                /* a click on empty space closes the app menu */
        }
    } else if (up && pressed >= 0) {
        int was = pressed;
        pressed = -1;
        draw_target(was);
        consumed = 1;
        if (was == over) {
            struct target g = targets[was];
            if (g.kind != T_POWER && menu_open)
                menu_set(0);
            screen_present();
            activate(&g);
        }
    }
    screen_present();
    return consumed;
}

int desktop_input_key(const struct key_event *k)
{
    if (!k->pressed || k->key != KEY_ESC)
        return 0;
    if (menu_open)
        menu_set(0);
    else if (grid_open && desktop_visible())
        set_grid(0);
    else
        return 0;
    screen_present();
    return 1;
}

void desktop_tick(void)
{
    uint32_t ticks;
    if (!started || !rtc || dev_ioctl(rtc, RTC_GET_TICKS, &ticks) < 0 || ticks == last_tick)
        return;
    last_tick = ticks;
    struct rtc_time now;
    dev_read(rtc, &now, sizeof(now));
    if (now.minute == clock_shown.minute && now.hour == clock_shown.hour &&
        now.day == clock_shown.day && clock_shown.month)
        return;
    clock_shown = now;
    log_clock();
    draw_clock();
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
    case WM_EVENT_THEME:
    case WM_EVENT_POINTER:      /* too many to log; clicks already are */
        break;
    }
}

void desktop_handle_events(void)
{
    struct wm_event ev;
    if (running && running->lkx)
        return;                 /* a ring 3 app takes its own (SYS_WAIT_EVENT) */
    while (wm_is_open() && wm_poll_event(&ev)) {
        if (ev.type == WM_EVENT_CLOSE) {
            kprintf("wm: close\n");
            close_app();
        } else if (running && running->event) {
            running->event(&ev);
        } else {
            log_event(&ev);
        }
    }
}

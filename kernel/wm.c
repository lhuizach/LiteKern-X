#include "kernel/wm.h"
#include "kernel/text.h"
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/string.h"
#include "kernel/theme.h"
#include "kernel/timing.h"

#define QUEUE 32
#define SHADOW 18           /* how far a window's shadow reaches */
#define GRIP 6              /* resize handle: this far outside the frame ... */
#define GRIP_IN 3           /* ... and this far inside */
#define CASCADE 32          /* a new window over an old one moves this far along */
#define DOUBLE_MS 400       /* two header clicks this close: maximise / restore */
#define DRAG_START 4        /* px a maximised window must be dragged before it lets go */

/* Buttons: the app's, then the window's own three. */
#define B_MIN   (WM_MAX_BUTTONS + 0)
#define B_MAX   (WM_MAX_BUTTONS + 1)
#define B_CLOSE (WM_MAX_BUTTONS + 2)
#define NB      (WM_MAX_BUTTONS + 3)
#define NONE    (-1)

struct button {
    enum wm_side side;
    enum wm_icon icon;
    char label[16];
    int id;
};

struct wm_window {
    int used;
    char title[WM_TITLE_MAX];
    struct button buttons[WM_MAX_BUTTONS];
    int nbuttons;
    struct gfx_rect r;          /* on screen */
    struct gfx_rect normal;     /* restored size and place */
    int maximised, minimised;
    int hidden;                 /* left out while an animation draws it instead */
    int min_w, min_h;
    uint32_t *buf;              /* the content's pixels: capacity buf_w x buf_h */
    int buf_w, buf_h;
    uint32_t buf_bytes;
    struct gfx_surface content;
    struct wm_event queue[QUEUE];
    int qhead, qtail;
    const void *owner;
};

static struct wm_window wins[WM_MAX];
static struct wm_window *z[WM_MAX];     /* back to front */
static int nz;
static struct wm_window *focus;
static struct gfx_rect area;            /* where windows live */

/* Pointer state. */
static uint8_t last_buttons;
static struct wm_window *hover_win, *press_win, *capture, *ptr_win;
static int hover_btn = NONE, press_btn = NONE;
static struct wm_window *drag_win, *resize_win;
static int drag_dx, drag_dy, drag_x0, drag_y0, drag_free;
static int resize_edges;
static struct gfx_rect resize_r0;
static int resize_x0, resize_y0;
static struct wm_window *last_header_win;
static uint32_t last_header_ms;

enum { E_LEFT = 1, E_RIGHT = 2, E_TOP = 4, E_BOTTOM = 8 };

static const struct theme *T(void)
{
    return theme_get();
}

static void copy(char *dst, const char *src, int max)
{
    int i = 0;
    for (; src && src[i] && i < max - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int inside(struct gfx_rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* --- geometry ------------------------------------------------------------------- */

static int inset(const struct wm_window *w)
{
    return w->maximised ? 0 : 1;            /* the 1 px frame */
}

static int radius(const struct wm_window *w)
{
    return w->maximised ? 0 : WM_RADIUS;
}

static struct gfx_rect header_rect(const struct wm_window *w, struct gfx_rect r)
{
    int i = inset(w);
    return (struct gfx_rect){ r.x + i, r.y + i, r.w - 2 * i, T()->headerbar_h };
}

static struct gfx_rect content_rect(const struct wm_window *w, struct gfx_rect r)
{
    int i = inset(w), hh = T()->headerbar_h;
    return (struct gfx_rect){ r.x + i, r.y + i + hh, r.w - 2 * i, r.h - 2 * i - hh };
}

/* Where each header button is, for the window at r: the window's three at
 * the far right, then the app's right buttons, its left buttons on the left. */
static void layout(const struct wm_window *w, struct gfx_rect r, struct gfx_rect out[NB])
{
    const struct theme *t = T();
    struct gfx_rect h = header_rect(w, r);
    int size = t->button_size, top = h.y + (h.h - size) / 2;
    int left = h.x + t->spacing, right = h.x + h.w - t->spacing;
    for (int b = B_CLOSE; b >= B_MIN; b--) {
        out[b] = (struct gfx_rect){ right - size, top, size, size };
        right -= size + (b == B_MIN ? t->spacing * 2 : 2);
    }
    for (int i = 0; i < WM_MAX_BUTTONS; i++) {
        if (i >= w->nbuttons) {
            out[i] = (struct gfx_rect){ 0, 0, 0, 0 };
            continue;
        }
        const struct button *b = &w->buttons[i];
        int bw = b->icon != WM_ICON_NONE ? size : text_width(b->label, TEXT_BOLD) + 2 * t->margin;
        if (b->side == WM_LEFT) {
            out[i] = (struct gfx_rect){ left, top, bw, size };
            left += bw + t->spacing;
        } else {
            out[i] = (struct gfx_rect){ right - bw, top, bw, size };
            right -= bw + t->spacing;
        }
    }
}

struct gfx_rect wm_bounds(const struct wm_window *w)
{
    struct gfx_rect r = w->r;
    int m = w->maximised ? 0 : SHADOW + 6;
    return (struct gfx_rect){ r.x - m, r.y - m, r.w + 2 * m, r.h + 2 * m };
}

static void damage(const struct wm_window *w)
{
    shell_damage(wm_bounds(w));
}

static void damage_button(const struct wm_window *w, int b)
{
    struct gfx_rect rs[NB];
    if (!w || b == NONE || w->minimised)
        return;
    layout(w, w->r, rs);
    shell_damage(rs[b]);
}

/* --- events --------------------------------------------------------------------- */

static void push(struct wm_window *w, struct wm_event ev)
{
    ev.time_ms = uptime_ms();
    /* A plain move replaces a plain move still waiting, and a resize a
     * resize: only the latest matters, and the queue stays short. */
    if (w->qhead != w->qtail) {
        struct wm_event *last = &w->queue[(w->qhead - 1) % QUEUE];
        if ((ev.type == WM_EVENT_POINTER && !ev.changed && last->type == WM_EVENT_POINTER &&
             !last->changed) ||
            (ev.type == WM_EVENT_RESIZE && last->type == WM_EVENT_RESIZE)) {
            *last = ev;
            return;
        }
    }
    if (w->qhead - w->qtail == QUEUE)
        w->qtail++;                 /* full: drop the oldest */
    w->queue[w->qhead++ % QUEUE] = ev;
}

int wm_poll_event(struct wm_window *w, struct wm_event *ev)
{
    if (!w || w->qtail == w->qhead)
        return 0;
    *ev = w->queue[w->qtail++ % QUEUE];
    return 1;
}

int wm_has_event(const struct wm_window *w)
{
    return w && w->qtail != w->qhead;
}

void wm_request_close(struct wm_window *w)
{
    if (w)
        push(w, (struct wm_event){ .type = WM_EVENT_CLOSE });
}

/* --- size ----------------------------------------------------------------------- */

/* The window's rectangle changed: the content follows, newly uncovered parts
 * start as the window background, and the app is told to lay out again. */
static void apply_size(struct wm_window *w)
{
    struct gfx_rect c = content_rect(w, w->r);
    if (c.w > w->buf_w)
        c.w = w->buf_w;
    if (c.h > w->buf_h)
        c.h = w->buf_h;
    if (c.w == w->content.w && c.h == w->content.h)
        return;
    struct gfx_surface full = { w->buf, w->buf_w, w->buf_h, w->buf_w };
    uint32_t bg = T()->window_bg;
    if (c.w > w->content.w)
        gfx_fill_rect(&full, w->content.w, 0, c.w - w->content.w, c.h, bg);
    if (c.h > w->content.h)
        gfx_fill_rect(&full, 0, w->content.h, c.w, c.h - w->content.h, bg);
    w->content.w = c.w;
    w->content.h = c.h;
    push(w, (struct wm_event){ .type = WM_EVENT_RESIZE, .x = c.w, .y = c.h });
}

static struct gfx_rect clamp_rect(const struct wm_window *w, struct gfx_rect r)
{
    if (r.w > area.w)
        r.w = area.w;
    if (r.h > area.h)
        r.h = area.h;
    if (r.w < w->min_w)
        r.w = w->min_w;
    if (r.h < w->min_h)
        r.h = w->min_h;
    if (r.y < area.y)
        r.y = area.y;
    if (r.y > area.y + area.h - T()->headerbar_h)
        r.y = area.y + area.h - T()->headerbar_h;
    if (r.x > area.x + area.w - 80)
        r.x = area.x + area.w - 80;
    if (r.x + r.w < area.x + 80)
        r.x = area.x + 80 - r.w;
    return r;
}

void wm_move_resize(struct wm_window *w, struct gfx_rect r)
{
    if (!w)
        return;
    damage(w);
    w->maximised = 0;
    w->r = clamp_rect(w, r);
    w->normal = w->r;
    apply_size(w);
    damage(w);
}

void wm_set_maximised(struct wm_window *w, int maximised)
{
    if (!w || w->maximised == !!maximised)
        return;
    damage(w);
    if (maximised) {
        w->normal = w->r;
        w->maximised = 1;
        w->r = area;
    } else {
        w->maximised = 0;
        w->r = clamp_rect(w, w->normal);
    }
    apply_size(w);
    damage(w);
}

/* --- creating and closing -------------------------------------------------------------- */

void wm_set_area(struct gfx_rect a)
{
    area = a;
}

struct gfx_rect wm_area(void)
{
    return area;
}

static void z_remove(struct wm_window *w)
{
    for (int i = 0; i < nz; i++)
        if (z[i] == w) {
            for (int k = i; k < nz - 1; k++)
                z[k] = z[k + 1];
            nz--;
            return;
        }
}

/* Focus the front window that isn't minimised (after one went away). */
static void refocus(void)
{
    struct wm_window *top = wm_top_visible();
    if (top != focus) {
        if (focus)
            damage(focus);
        focus = top;
        if (focus)
            damage(focus);
    }
    shell_window_focus_changed();
}

struct wm_window *wm_create(const char *title, int w, int h, int min_w, int min_h)
{
    struct wm_window *win = 0;
    for (int i = 0; i < WM_MAX && !win; i++)
        if (!wins[i].used)
            win = &wins[i];
    if (area.w <= 0 && screen_ready()) {    /* before the shell set it (the self-tests) */
        struct gfx_surface *scr = screen_surface();
        area = (struct gfx_rect){ 0, T()->topbar_h, scr->w, scr->h - T()->topbar_h };
    }
    if (!win || area.w <= 0)
        return 0;
    /* The buffer: big enough for the largest content (maximised). */
    int bw = area.w, bh = area.h - T()->headerbar_h;
    uint32_t bytes = (uint32_t)(bw * bh * 4);
    uint32_t phys = pmm_alloc_contiguous((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!phys)
        return 0;
    memset(win, 0, sizeof(*win));
    win->used = 1;
    copy(win->title, title, WM_TITLE_MAX);
    win->buf = (uint32_t *)phys;
    win->buf_w = bw;
    win->buf_h = bh;
    win->buf_bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    win->min_w = min_w > 0 ? min_w : WM_MIN_W;
    win->min_h = min_h > 0 ? min_h : WM_MIN_H;
    win->content = (struct gfx_surface){ win->buf, 0, 0, bw };

    /* Centred; moved along while another window already sits there. */
    struct gfx_rect r = { area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h };
    for (int tries = 0; tries < WM_MAX; tries++) {
        int taken = 0;
        for (int i = 0; i < nz; i++)
            taken |= !z[i]->minimised && z[i]->r.x == r.x && z[i]->r.y == r.y;
        if (!taken)
            break;
        r.x += CASCADE;
        r.y += CASCADE;
    }
    win->r = clamp_rect(win, r);
    win->normal = win->r;
    apply_size(win);
    win->qhead = win->qtail = 0;            /* no RESIZE for the first size */
    struct gfx_surface full = { win->buf, bw, bh, bw };
    gfx_fill_rect(&full, 0, 0, win->content.w, win->content.h, T()->window_bg);

    z[nz++] = win;
    if (focus)
        damage(focus);
    focus = win;
    damage(win);
    kprintf("wm: open %s at %d,%d %dx%d\n", win->title, win->r.x, win->r.y, win->r.w, win->r.h);
    shell_window_focus_changed();
    return win;
}

void wm_destroy(struct wm_window *w)
{
    if (!w || !w->used)
        return;
    damage(w);
    z_remove(w);
    for (uint32_t off = 0; off < w->buf_bytes; off += PAGE_SIZE)
        pmm_free((uint32_t)w->buf + off);
    w->used = 0;
    if (hover_win == w)
        hover_win = 0, hover_btn = NONE;
    if (press_win == w)
        press_win = 0, press_btn = NONE;
    if (capture == w)
        capture = 0;
    if (ptr_win == w)
        ptr_win = 0;
    if (drag_win == w)
        drag_win = 0;
    if (resize_win == w)
        resize_win = 0;
    if (last_header_win == w)
        last_header_win = 0;
    if (focus == w)
        focus = 0;
    refocus();
}

/* --- small accessors ------------------------------------------------------------------ */

void wm_set_title(struct wm_window *w, const char *title)
{
    if (!w)
        return;
    copy(w->title, title, WM_TITLE_MAX);
    if (!w->minimised)
        shell_damage(header_rect(w, w->r));
}

const char *wm_title(const struct wm_window *w)
{
    return w ? w->title : "";
}

int wm_add_button(struct wm_window *w, enum wm_side side, enum wm_icon icon, const char *label, int id)
{
    if (w->nbuttons == WM_MAX_BUTTONS)
        return -ENOMEM;
    struct button *b = &w->buttons[w->nbuttons++];
    b->side = side;
    b->icon = icon;
    copy(b->label, label, sizeof(b->label));
    b->id = id;
    if (!w->minimised)
        shell_damage(header_rect(w, w->r));
    return 0;
}

void wm_clear_buttons(struct wm_window *w)
{
    w->nbuttons = 0;
    if (hover_win == w && hover_btn < WM_MAX_BUTTONS)
        hover_btn = NONE;
    if (press_win == w && press_btn < WM_MAX_BUTTONS)
        press_btn = NONE;
    if (!w->minimised)
        shell_damage(header_rect(w, w->r));
}

void wm_set_owner(struct wm_window *w, const void *owner)
{
    w->owner = owner;
}

const void *wm_owner(const struct wm_window *w)
{
    return w ? w->owner : 0;
}

struct gfx_surface *wm_content(struct wm_window *w)
{
    return &w->content;
}

uint32_t *wm_buffer(struct wm_window *w, uint32_t *bytes)
{
    *bytes = w->buf_bytes;
    return w->buf;
}

void wm_damage(struct wm_window *w, int x, int y, int width, int height)
{
    if (!w || w->minimised)
        return;
    struct gfx_rect c = content_rect(w, w->r);
    struct gfx_rect d = gfx_rect_intersect((struct gfx_rect){ c.x + x, c.y + y, width, height }, c);
    if (!gfx_rect_empty(d))
        shell_damage(d);
}

void wm_present(void)
{
    shell_flush();
}

void wm_theme_changed(void)
{
    for (int i = 0; i < nz; i++) {
        damage(z[i]);
        push(z[i], (struct wm_event){ .type = WM_EVENT_THEME });
    }
}

struct gfx_rect wm_rect(const struct wm_window *w)
{
    return w->r;
}

struct gfx_rect wm_normal_rect(const struct wm_window *w)
{
    return w->maximised ? w->normal : w->r;
}

int wm_is_maximised(const struct wm_window *w)
{
    return w->maximised;
}

int wm_is_minimised(const struct wm_window *w)
{
    return w->minimised;
}

struct wm_window *wm_focused(void)
{
    return focus;
}

int wm_count(void)
{
    return nz;
}

struct wm_window *wm_at(int i)
{
    return i >= 0 && i < nz ? z[i] : 0;
}

struct wm_window *wm_top_visible(void)
{
    for (int i = nz - 1; i >= 0; i--)
        if (!z[i]->minimised)
            return z[i];
    return 0;
}

void wm_focus(struct wm_window *w)
{
    if (!w)
        return;
    if (w->minimised)
        w->minimised = 0;
    if (focus == w && z[nz - 1] == w) {
        damage(w);
        return;
    }
    z_remove(w);
    z[nz++] = w;
    if (focus)
        damage(focus);
    focus = w;
    damage(w);
    shell_window_focus_changed();
}

void wm_set_minimised(struct wm_window *w, int minimised)
{
    if (!w || w->minimised == !!minimised)
        return;
    if (minimised) {
        damage(w);
        w->minimised = 1;
        if (capture == w)
            capture = 0;
        if (focus == w)
            focus = 0;
        refocus();
    } else {
        wm_focus(w);
    }
}

void wm_set_hidden(struct wm_window *w, int hidden)
{
    if (!w || w->hidden == !!hidden)
        return;
    w->hidden = !!hidden;
    damage(w);
}

int wm_grabbed(void)
{
    return drag_win || resize_win || press_win || capture;
}

/* --- drawing -------------------------------------------------------------------------- */

static void draw_icon(struct gfx_surface *s, enum wm_icon icon, int cx, int cy, uint32_t c)
{
    switch (icon) {
    case WM_ICON_BACK:
        for (int i = 0; i < 2; i++) {
            gfx_line(s, cx + 3 + i, cy - 5, cx - 2 + i, cy, c);
            gfx_line(s, cx - 2 + i, cy, cx + 3 + i, cy + 5, c);
        }
        break;
    case WM_ICON_UP:
        for (int i = 0; i < 2; i++) {
            gfx_line(s, cx - 5, cy + 3 + i, cx, cy - 2 + i, c);
            gfx_line(s, cx, cy - 2 + i, cx + 5, cy + 3 + i, c);
        }
        break;
    case WM_ICON_ADD:
        gfx_fill_rect(s, cx - 6, cy - 1, 13, 2, c);
        gfx_fill_rect(s, cx - 1, cy - 6, 2, 13, c);
        break;
    case WM_ICON_NONE:
        break;
    }
}

/* The window's own buttons: small round ones, Adwaita's window controls. */
static void draw_control(struct gfx_surface *s, int b, int maximised, int cx, int cy, uint32_t bg,
                         uint32_t fg)
{
    gfx_fill_circle(s, cx, cy, 12, bg);
    switch (b) {
    case B_MIN:
        gfx_fill_rect(s, cx - 4, cy + 3, 9, 2, fg);
        break;
    case B_MAX:
        if (maximised) {                    /* restore: two overlapping squares */
            gfx_rect_outline(s, cx - 4, cy - 2, 7, 7, fg);
            gfx_fill_rect(s, cx - 2, cy - 5, 7, 1, fg);
            gfx_fill_rect(s, cx + 4, cy - 5, 1, 7, fg);
        } else {
            gfx_rect_outline(s, cx - 4, cy - 4, 9, 9, fg);
            gfx_rect_outline(s, cx - 3, cy - 3, 7, 7, fg);
        }
        break;
    case B_CLOSE:
        for (int k = 0; k < 2; k++) {
            gfx_line(s, cx - 4 + k, cy - 4, cx + 4 + k, cy + 4, fg);
            gfx_line(s, cx + 4 + k, cy - 4, cx - 4 + k, cy + 4, fg);
        }
        break;
    }
}

static void draw_header(struct gfx_view *v, struct wm_window *w, struct gfx_rect r, int live)
{
    const struct theme *t = T();
    struct gfx_rect h = header_rect(w, r);
    int rad = radius(w) ? radius(w) - 1 : 0, focused = w == focus || !live;
    struct gfx_surface *s = &v->s;
    int ox = v->ox, oy = v->oy;
    gfx_fill_round_rect(s, h.x - ox, h.y - oy, h.w, h.h + rad, rad, t->headerbar_bg);
    gfx_fill_rect(s, h.x - ox, h.y + h.h - 1 - oy, h.w, 1, t->headerbar_border);

    struct gfx_rect rs[NB];
    layout(w, r, rs);
    uint32_t fg = focused ? t->fg : t->fg_dim;
    for (int b = 0; b < NB; b++) {
        if (b < WM_MAX_BUTTONS && b >= w->nbuttons)
            continue;
        int lit = live && hover_win == w && hover_btn == b;
        int down = live && press_win == w && press_btn == b && lit;
        uint32_t bg = down ? t->button_active : lit ? t->button_hover : 0;
        int cx = rs[b].x + rs[b].w / 2 - ox, cy = rs[b].y + rs[b].h / 2 - oy;
        if (b >= B_MIN) {
            draw_control(s, b, w->maximised, cx, cy, bg ? bg : t->button_bg, fg);
            continue;
        }
        if (bg)     /* flat header-bar buttons only show a background when hovered */
            gfx_fill_round_rect(s, rs[b].x - ox, rs[b].y - oy, rs[b].w, rs[b].h, t->radius, bg);
        const struct button *bt = &w->buttons[b];
        if (bt->icon != WM_ICON_NONE)
            draw_icon(s, bt->icon, cx, cy, fg);
        else
            text_draw(s, cx - text_width(bt->label, TEXT_BOLD) / 2, cy - text_height(TEXT_BOLD) / 2,
                      bt->label, TEXT_BOLD, fg);
    }

    /* The title, centred, cut short between the buttons if it must. */
    int lmax = h.x + t->spacing, rmin = rs[B_MIN].x;
    for (int b = 0; b < w->nbuttons; b++) {
        if (w->buttons[b].side == WM_LEFT && rs[b].x + rs[b].w > lmax)
            lmax = rs[b].x + rs[b].w;
        if (w->buttons[b].side == WM_RIGHT && rs[b].x < rmin)
            rmin = rs[b].x;
    }
    int half = (h.x + h.w / 2 - lmax < rmin - (h.x + h.w / 2) ? h.x + h.w / 2 - lmax
                                                               : rmin - (h.x + h.w / 2)) - t->spacing;
    int tw = text_width(w->title, TEXT_BOLD);
    if (half > 0) {
        int maxw = 2 * half, x = h.x + (h.w - (tw < maxw ? tw : maxw)) / 2;
        text_draw_fit(s, x - ox, h.y + (h.h - text_height(TEXT_BOLD)) / 2 - oy, w->title, maxw,
                      TEXT_BOLD, fg);
    }
}

void wm_draw_one(struct gfx_surface *dst, int ox, int oy, struct wm_window *w, struct gfx_rect r)
{
    const struct theme *t = T();
    struct gfx_view v = { *dst, ox, oy };
    int rad = r.w == w->r.w && r.h == w->r.h ? radius(w) : 0;
    int ri = rad ? rad - 1 : 0, in = inset(w);
    int live = r.x == w->r.x && r.y == w->r.y && r.w == w->r.w && r.h == w->r.h;

    if (in) {           /* the frame: a 1 px line, faint, as libadwaita draws it */
        uint32_t frame = gfx_mix(t->ink, t->window_bg, t->light ? 40 : 34);
        struct gfx_rect strips[4] = { { r.x, r.y, r.w, rad + 1 }, { r.x, r.y + r.h - rad - 1, r.w, rad + 1 },
                                      { r.x, r.y, 1, r.h }, { r.x + r.w - 1, r.y, 1, r.h } };
        for (int i = 0; i < 4; i++) {
            struct gfx_view c = gfx_view_clip(&v, strips[i]);
            if (c.s.w)
                gfx_fill_round_rect(&c.s, r.x - c.ox, r.y - c.oy, r.w, r.h, rad, frame);
        }
    }
    struct gfx_rect hr = header_rect(w, r);
    struct gfx_view hv = gfx_view_clip(&v, hr);
    if (hv.s.w)
        draw_header(&hv, w, r, live);

    /* The content, its bottom corners rounded over the window background. */
    struct gfx_rect c = content_rect(w, r);
    int cw = c.w < w->content.w ? c.w : w->content.w, ch = c.h < w->content.h ? c.h : w->content.h;
    if (ri) {
        struct gfx_view bv = gfx_view_clip(&v, (struct gfx_rect){ c.x, c.y + c.h - ri, c.w, ri });
        if (bv.s.w)
            gfx_fill_round_rect(&bv.s, c.x - bv.ox, c.y + c.h - 2 * ri - bv.oy, c.w, 2 * ri, ri, t->window_bg);
    }
    gfx_blit(dst, c.x - ox, c.y - oy, &w->content, 0, 0, cw, ch - ri);
    gfx_blit(dst, c.x + ri - ox, c.y + ch - ri - oy, &w->content, ri, ch - ri, cw - 2 * ri, ri);
    if (cw < c.w)       /* never bigger than the buffer, but keep it tidy */
        gfx_fill_rect(dst, c.x + cw - ox, c.y - oy, c.w - cw, c.h, t->window_bg);
}

void wm_draw(struct gfx_surface *dst, int ox, int oy)
{
    struct gfx_rect view = { ox, oy, dst->w, dst->h };
    for (int i = 0; i < nz; i++) {
        struct wm_window *w = z[i];
        if (w->minimised || w->hidden || gfx_rect_empty(gfx_rect_intersect(wm_bounds(w), view)))
            continue;
        if (!w->maximised)
            gfx_shadow(dst, w->r.x - ox, w->r.y - oy, w->r.w, w->r.h, WM_RADIUS, SHADOW,
                       w == focus ? 150 : 90);
        if (!gfx_rect_empty(gfx_rect_intersect(w->r, view)))
            wm_draw_one(dst, ox, oy, w, w->r);
    }
}

/* --- input ------------------------------------------------------------------------------ */

void wm_input_key(const struct key_event *key)
{
    if (!focus || focus->minimised || !key->pressed)
        return;
    push(focus, (struct wm_event){ .type = WM_EVENT_KEY, .key = *key });
}

/* Which edges of w a point is on (for resizing), or 0. */
static int edges_at(const struct wm_window *w, int x, int y)
{
    if (w->maximised)
        return 0;
    struct gfx_rect r = w->r;
    if (x < r.x - GRIP || x >= r.x + r.w + GRIP || y < r.y - GRIP || y >= r.y + r.h + GRIP)
        return 0;
    int e = 0;
    if (x < r.x + GRIP_IN)
        e |= E_LEFT;
    if (x >= r.x + r.w - GRIP_IN)
        e |= E_RIGHT;
    if (y < r.y + GRIP_IN)
        e |= E_TOP;
    if (y >= r.y + r.h - GRIP_IN)
        e |= E_BOTTOM;
    /* The corners are easier to hit: within the corner radius of one. */
    if ((e & (E_LEFT | E_RIGHT)) && !(e & (E_TOP | E_BOTTOM))) {
        if (y < r.y + WM_RADIUS)
            e |= E_TOP;
        else if (y >= r.y + r.h - WM_RADIUS)
            e |= E_BOTTOM;
    }
    return e;
}

/* The front window at (x, y), counting the resize handles around it. */
static struct wm_window *window_at(int x, int y)
{
    for (int i = nz - 1; i >= 0; i--) {
        struct wm_window *w = z[i];
        if (!w->minimised && !w->hidden && (inside(w->r, x, y) || edges_at(w, x, y)))
            return w;
    }
    return 0;
}

static int button_at(const struct wm_window *w, int x, int y)
{
    struct gfx_rect rs[NB];
    layout(w, w->r, rs);
    for (int b = 0; b < NB; b++)
        if ((b >= B_MIN || b < w->nbuttons) && inside(rs[b], x, y))
            return b;
    return NONE;
}

static void set_hover(struct wm_window *w, int b)
{
    if (w == hover_win && b == hover_btn)
        return;
    damage_button(hover_win, hover_btn);
    hover_win = w;
    hover_btn = b;
    damage_button(hover_win, hover_btn);
}

/* The pointer moved away from ptr_win's content: one last update, so the
 * app can drop its hover highlight. */
static void leave(struct wm_window *now, int x, int y, uint8_t buttons)
{
    if (ptr_win && ptr_win != now) {
        struct gfx_rect c = content_rect(ptr_win, ptr_win->r);
        int rx = x - c.x, ry = y - c.y;
        if (rx >= 0 && rx < c.w && ry >= 0 && ry < c.h)
            ry = -1;                        /* covered by another window: outside */
        push(ptr_win, (struct wm_event){ .type = WM_EVENT_POINTER, .x = rx, .y = ry, .buttons = buttons });
    }
    ptr_win = now;
}

void wm_pointer_gone(void)
{
    set_hover(0, NONE);
    if (ptr_win)
        push(ptr_win, (struct wm_event){ .type = WM_EVENT_POINTER, .x = -1, .y = -1 });
    ptr_win = 0;
}

static void press_button(struct wm_window *w, int b)
{
    if (b == B_CLOSE)
        push(w, (struct wm_event){ .type = WM_EVENT_CLOSE });
    else if (b == B_MIN)
        shell_window_minimise(w);
    else if (b == B_MAX)
        shell_window_maximise(w, !w->maximised, w->r);
    else
        push(w, (struct wm_event){ .type = WM_EVENT_HEADER, .id = w->buttons[b].id });
}

static void drag_to(int x, int y)
{
    struct wm_window *w = drag_win;
    if (!drag_free) {
        if ((x - drag_x0) * (x - drag_x0) + (y - drag_y0) * (y - drag_y0) < DRAG_START * DRAG_START)
            return;
        drag_free = 1;
        if (w->maximised) {
            /* Let go of the maximised size under the pointer, keeping the
             * grab point at the same place across the title bar. */
            struct gfx_rect n = w->normal;
            int fx = (x - w->r.x) * 1024 / (w->r.w ? w->r.w : 1);
            damage(w);
            w->maximised = 0;
            n.x = x - n.w * fx / 1024;
            n.y = w->r.y;
            w->r = clamp_rect(w, n);
            drag_dx = x - w->r.x;
            drag_dy = y - w->r.y;
            apply_size(w);
            damage(w);
        }
    }
    struct gfx_rect r = w->r;
    r.x = x - drag_dx;
    r.y = y - drag_dy;
    r = clamp_rect(w, r);
    if (r.x == w->r.x && r.y == w->r.y)
        return;
    damage(w);
    w->r = r;
    w->normal = r;
    damage(w);
}

static void drag_end(int x, int y)
{
    struct wm_window *w = drag_win;
    drag_win = 0;
    if (!drag_free)
        return;
    kprintf("wm: %s moved to %d,%d\n", w->title, w->r.x, w->r.y);
    /* Snap: the top edge maximises, the side edges take half the screen. */
    if (y <= area.y + 1) {
        shell_window_maximise(w, 1, w->r);
    } else if (x <= area.x + 1 || x >= area.x + area.w - 2) {
        struct gfx_rect half = { x <= area.x + 1 ? area.x : area.x + area.w / 2, area.y, area.w / 2,
                                 area.h };
        wm_move_resize(w, half);
        kprintf("wm: %s snapped to the %s half\n", w->title, half.x == area.x ? "left" : "right");
    }
}

static void resize_to(int x, int y)
{
    struct wm_window *w = resize_win;
    struct gfx_rect r = resize_r0;
    int dx = x - resize_x0, dy = y - resize_y0;
    if (resize_edges & E_LEFT) {
        int nw = r.w - dx < w->min_w ? w->min_w : r.w - dx;
        r.x += r.w - nw;
        r.w = nw;
    }
    if (resize_edges & E_RIGHT)
        r.w = r.w + dx < w->min_w ? w->min_w : r.w + dx;
    if (resize_edges & E_TOP) {
        int nh = r.h - dy < w->min_h ? w->min_h : r.h - dy;
        if (r.y + r.h - nh < area.y)
            nh = r.y + r.h - area.y;
        r.y += r.h - nh;
        r.h = nh;
    }
    if (resize_edges & E_BOTTOM)
        r.h = r.h + dy < w->min_h ? w->min_h : r.h + dy;
    if (r.w > area.w)
        r.w = area.w;
    if (r.h > area.h)
        r.h = area.h;
    if (r.x == w->r.x && r.y == w->r.y && r.w == w->r.w && r.h == w->r.h)
        return;
    damage(w);
    w->r = r;
    w->normal = r;
    apply_size(w);
    damage(w);
}

int wm_input_mouse(int x, int y, uint8_t buttons)
{
    int down = (buttons & MOUSE_LEFT) && !(last_buttons & MOUSE_LEFT);
    int up = !(buttons & MOUSE_LEFT) && (last_buttons & MOUSE_LEFT);
    uint8_t changed = buttons ^ last_buttons;
    last_buttons = buttons;

    if (drag_win) {
        if (up)
            drag_end(x, y);
        else
            drag_to(x, y);
        return 1;
    }
    if (resize_win) {
        if (up) {
            kprintf("wm: %s resized to %dx%d\n", resize_win->title, resize_win->r.w, resize_win->r.h);
            resize_win = 0;
        } else {
            resize_to(x, y);
        }
        return 1;
    }
    if (press_win) {                        /* a header button is held */
        struct wm_window *w = press_win;
        int b = press_btn, over = window_at(x, y) == w ? button_at(w, x, y) : NONE;
        set_hover(over == b ? w : 0, over == b ? b : NONE);
        if (up) {
            press_win = 0;
            press_btn = NONE;
            damage_button(w, b);
            if (over == b)
                press_button(w, b);
        }
        return 1;
    }
    if (capture) {                          /* a press in the content: it gets everything */
        struct gfx_rect c = content_rect(capture, capture->r);
        push(capture, (struct wm_event){ .type = WM_EVENT_POINTER, .x = x - c.x, .y = y - c.y,
                                         .buttons = buttons, .changed = changed });
        if (!(buttons & (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE)))
            capture = 0;
        return 1;
    }

    struct wm_window *w = window_at(x, y);
    if (!w) {
        set_hover(0, NONE);
        leave(0, x, y, buttons);
        return 0;
    }
    struct gfx_rect c = content_rect(w, w->r);
    int edges = edges_at(w, x, y), in_content = inside(c, x, y) && !edges;
    int b = !edges && !in_content ? button_at(w, x, y) : NONE;
    set_hover(b != NONE ? w : 0, b);
    leave(in_content ? w : 0, x, y, buttons);

    if (down && w != focus)
        wm_focus(w);                        /* the click also goes on to what's under it */
    if (in_content) {
        push(w, (struct wm_event){ .type = WM_EVENT_POINTER, .x = x - c.x, .y = y - c.y,
                                   .buttons = buttons, .changed = changed });
        if (down) {
            push(w, (struct wm_event){ .type = WM_EVENT_CLICK, .x = x - c.x, .y = y - c.y,
                                       .buttons = buttons });
            capture = w;
        }
        return 1;
    }
    if (!down)
        return 1;
    if (edges) {
        resize_win = w;
        resize_edges = edges;
        resize_r0 = w->r;
        resize_x0 = x;
        resize_y0 = y;
    } else if (b != NONE) {
        press_win = w;
        press_btn = b;
        damage_button(w, b);
    } else if (last_header_win == w && uptime_ms() - last_header_ms < DOUBLE_MS) {
        last_header_win = 0;
        shell_window_maximise(w, !w->maximised, w->r);
    } else {                                /* the header bar: drag to move */
        last_header_win = w;
        last_header_ms = uptime_ms();
        drag_win = w;
        drag_free = 0;
        drag_x0 = x;
        drag_y0 = y;
        drag_dx = x - w->r.x;
        drag_dy = y - w->r.y;
    }
    return 1;
}

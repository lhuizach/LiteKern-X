#include "kernel/wm.h"
#include "kernel/console.h"
#include "kernel/errno.h"
#include "kernel/font.h"
#include "kernel/screen.h"
#include "kernel/theme.h"
#include "kernel/timing.h"

#define QUEUE 32
#define CLOSE_ID (-1)

struct button {
    enum wm_side side;
    enum wm_icon icon;
    char label[16];
    int id;
    struct gfx_rect r;      /* screen coordinates, set by layout() */
};

static int open;
static char title[WM_TITLE_MAX];
static struct button buttons[WM_MAX_BUTTONS + 1];   /* + the close button */
static int nbuttons;
static struct gfx_surface content;
static struct wm_event queue[QUEUE];
static int qhead, qtail;
static int hover = -2, pressed = -2;                /* button index; -2 = none */
static uint8_t last_buttons;

/* The window starts under the shell's top bar (kernel/desktop.h). */
static int win_top(void)
{
    return theme_get()->topbar_h;
}

/* The first row of the content area, on the screen. */
static int content_top(void)
{
    return win_top() + theme_get()->headerbar_h;
}

static void copy(char *dst, const char *src, int max)
{
    int i = 0;
    for (; src && src[i] && i < max - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static void push(struct wm_event ev)
{
    ev.time_ms = uptime_ms();
    /* A plain move replaces a plain move still waiting: only the latest
     * position matters, and the queue stays short. */
    if (ev.type == WM_EVENT_POINTER && !ev.changed && qhead != qtail) {
        struct wm_event *last = &queue[(qhead - 1) % QUEUE];
        if (last->type == WM_EVENT_POINTER && !last->changed) {
            *last = ev;
            return;
        }
    }
    if (qhead - qtail == QUEUE)
        qtail++;            /* full: drop the oldest */
    queue[qhead++ % QUEUE] = ev;
}

/* Place the buttons: app buttons left-to-right on the left, right-to-left on
 * the right, then the close button at the far right. */
static void layout(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    int top = win_top() + (t->headerbar_h - t->button_size) / 2;
    int left = t->spacing, right = s->w - t->spacing;

    /* The close button: a 24 px circle in a button-size box, far right. */
    struct button *c = &buttons[nbuttons];
    c->side = WM_RIGHT;
    c->icon = WM_ICON_NONE;
    c->label[0] = 'x';
    c->label[1] = '\0';
    c->id = CLOSE_ID;
    c->r = (struct gfx_rect){ right - t->button_size, top, t->button_size, t->button_size };
    right -= t->button_size + t->spacing;

    for (int i = 0; i < nbuttons; i++) {
        struct button *b = &buttons[i];
        int w = b->icon != WM_ICON_NONE ? t->button_size
                                        : gfx_text_width(b->label) + 2 * t->margin;
        if (b->side == WM_LEFT) {
            b->r = (struct gfx_rect){ left, top, w, t->button_size };
            left += w + t->spacing;
        } else {
            b->r = (struct gfx_rect){ right - w, top, w, t->button_size };
            right -= w + t->spacing;
        }
    }
}

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

static void draw_button(struct gfx_surface *s, int i)
{
    const struct theme *t = theme_get();
    struct button *b = &buttons[i];
    int cx = b->r.x + b->r.w / 2, cy = b->r.y + b->r.h / 2;
    uint32_t bg = pressed == i ? t->button_active : hover == i ? t->button_hover : 0;

    gfx_fill_rect(s, b->r.x, b->r.y, b->r.w, b->r.h, t->headerbar_bg);
    if (b->id == CLOSE_ID) {
        /* Adwaita's window control: a small round button with an x. */
        gfx_fill_circle(s, cx, cy, 12, bg ? bg : t->button_bg);
        for (int k = 0; k < 2; k++) {
            gfx_line(s, cx - 4 + k, cy - 4, cx + 4 + k, cy + 4, t->fg);
            gfx_line(s, cx + 4 + k, cy - 4, cx - 4 + k, cy + 4, t->fg);
        }
        return;
    }
    if (bg)     /* flat header-bar buttons only show a background when hovered */
        gfx_fill_round_rect(s, b->r.x, b->r.y, b->r.w, b->r.h, t->radius, bg);
    if (b->icon != WM_ICON_NONE)
        draw_icon(s, b->icon, cx, cy, t->fg);
    else
        gfx_text(s, cx - gfx_text_width(b->label) / 2, cy - FONT_H / 2, b->label, t->fg,
                 GFX_TRANSPARENT);
}

static void draw_header(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    int y = win_top();
    gfx_fill_rect(s, 0, y, s->w, t->headerbar_h - 1, t->headerbar_bg);
    gfx_fill_rect(s, 0, y + t->headerbar_h - 1, s->w, 1, t->headerbar_border);
    for (int i = 0; i <= nbuttons; i++)
        draw_button(s, i);
    int tw = gfx_text_width(title);
    gfx_text(s, (s->w - tw) / 2, y + (t->headerbar_h - FONT_H) / 2, title, t->fg, GFX_TRANSPARENT);
    /* Faux bold, as GNOME titles are: draw it again one pixel to the right. */
    gfx_text(s, (s->w - tw) / 2 + 1, y + (t->headerbar_h - FONT_H) / 2, title, t->fg, GFX_TRANSPARENT);
    screen_damage(0, y, s->w, t->headerbar_h);
}

void wm_open(const char *name)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    console_set_visible(0);
    open = 1;
    copy(title, name, WM_TITLE_MAX);
    nbuttons = 0;
    qhead = qtail = 0;
    hover = pressed = -2;
    last_buttons = 0;       /* apps open on a release: nothing is held */
    content = (struct gfx_surface){ s->px + content_top() * s->stride, s->w,
                                    s->h - content_top(), s->stride };
    gfx_fill_rect(&content, 0, 0, content.w, content.h, t->window_bg);
    layout();
    draw_header();
    screen_damage_all();
    screen_present();
}

void wm_close(void)
{
    if (!open)
        return;
    open = 0;
    hover = pressed = -2;
}

int wm_is_open(void)
{
    return open;
}

void wm_set_title(const char *name)
{
    copy(title, name, WM_TITLE_MAX);
    if (open) {
        draw_header();
        screen_present();
    }
}

int wm_add_button(enum wm_side side, enum wm_icon icon, const char *label, int id)
{
    if (nbuttons == WM_MAX_BUTTONS)
        return -ENOMEM;
    struct button *b = &buttons[nbuttons++];
    b->side = side;
    b->icon = icon;
    copy(b->label, label, sizeof(b->label));
    b->id = id;
    layout();
    if (open) {
        draw_header();
        screen_present();
    }
    return 0;
}

void wm_clear_buttons(void)
{
    nbuttons = 0;
    hover = pressed = -2;
    layout();
    if (open) {
        draw_header();
        screen_present();
    }
}

struct gfx_surface *wm_content(void)
{
    return &content;
}

void wm_damage(int x, int y, int w, int h)
{
    screen_damage(x, y + content_top(), w, h);
}

void wm_present(void)
{
    screen_present();
}

int wm_poll_event(struct wm_event *ev)
{
    if (qtail == qhead)
        return 0;
    *ev = queue[qtail++ % QUEUE];
    return 1;
}

void wm_input_key(const struct key_event *key)
{
    if (!open || !key->pressed)
        return;
    push((struct wm_event){ .type = WM_EVENT_KEY, .key = *key });
}

static int button_at(int x, int y)
{
    for (int i = 0; i <= nbuttons; i++) {
        struct gfx_rect r = buttons[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return i;
    }
    return -2;
}

static void redraw_button(int i)
{
    if (i < 0)
        return;
    draw_button(screen_surface(), i);
    screen_damage(buttons[i].r.x, buttons[i].r.y, buttons[i].r.w, buttons[i].r.h);
}

void wm_input_mouse(int x, int y, uint8_t mouse_buttons)
{
    if (!open)
        return;
    int down = (mouse_buttons & MOUSE_LEFT) && !(last_buttons & MOUSE_LEFT);
    int up = !(mouse_buttons & MOUSE_LEFT) && (last_buttons & MOUSE_LEFT);
    uint8_t changed = mouse_buttons ^ last_buttons;
    last_buttons = mouse_buttons;

    /* Every update reaches the app (for hover, press and drag), except a
     * press that belongs to a header button. */
    if (!(down && button_at(x, y) >= 0) && !(up && pressed != -2))
        push((struct wm_event){ .type = WM_EVENT_POINTER, .x = x, .y = y - content_top(),
                                .buttons = mouse_buttons, .changed = changed });

    int over = button_at(x, y);
    if (over != hover) {
        int old = hover;
        hover = over;
        redraw_button(old);
        redraw_button(hover);
    }

    if (down && over >= 0) {
        pressed = over;
        redraw_button(pressed);
    } else if (up && pressed != -2) {
        int was = pressed;
        pressed = -2;
        redraw_button(was);
        if (was == over) {      /* released on the same button: it's a press */
            if (buttons[was].id == CLOSE_ID)
                push((struct wm_event){ .type = WM_EVENT_CLOSE });
            else
                push((struct wm_event){ .type = WM_EVENT_HEADER, .id = buttons[was].id });
        }
    } else if (down && y >= content_top()) {
        push((struct wm_event){ .type = WM_EVENT_CLICK, .x = x, .y = y - content_top(),
                                .buttons = mouse_buttons });
    }
    screen_present();
}

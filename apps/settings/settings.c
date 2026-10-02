/* LiteKern X — Settings: the Appearance page (Phase 3 §5), like GNOME's.
 *
 * Style (dark or light, shown as two little window previews), accent colour
 * (GNOME's presets), and background (the wallpapers in the ramdisk, or
 * none). A click applies it at once: the kernel redraws the shell and sends
 * every window K86_EVENT_THEME. Not saved across a restart yet (Phase 5);
 * the build-time defaults come from `make STYLE= ACCENT= WALLPAPER=`. */
#include "kernel/string.h"
#include "kernel/text.h"
#include "kernel/theme.h"
#include "kernel/widget.h"
#include "sdk/kern86.h"

#define CARD_W 200
#define CARD_H 120
#define DOT_R 14
#define THUMB_W 160
#define THUMB_H 94
#define GAP 24

static struct k86_window win;
static struct gfx_surface canvas;
static struct k86_appearance app;
static uint32_t thumbs[K86_MAX_WALLPAPERS][THUMB_W * THUMB_H];
static int have_thumb[K86_MAX_WALLPAPERS];

/* Clickable things: kind + index, and where they are. */
enum kind { K_STYLE, K_ACCENT, K_WALLPAPER, K_NONE_WALLPAPER };
struct spot {
    enum kind kind;
    int index;
    struct gfx_rect r;
};
static struct spot spots[2 + K86_MAX_ACCENTS + K86_MAX_WALLPAPERS + 1];
static int nspots, hover = -1, pressed = -1;

static int x0, y_style, y_accent, y_background;

static void add_spot(enum kind k, int i, struct gfx_rect r)
{
    spots[nspots++] = (struct spot){ k, i, r };
}

static void layout(void)
{
    const struct theme *t = theme_get();
    int content_w = 2 * CARD_W + GAP;
    int items = 1 + app.nwallpapers;
    int bg_w = items * THUMB_W + (items - 1) * GAP;
    if (bg_w > content_w)
        content_w = bg_w;
    x0 = (canvas.w - content_w) / 2;
    int lh = text_height(TEXT_BOLD);
    y_style = 20;
    y_accent = y_style + lh + 10 + CARD_H + 10 + text_height(TEXT_BODY) + 24;
    y_background = y_accent + lh + 10 + 2 * DOT_R + 8 + 24;

    nspots = 0;
    for (int i = 0; i < 2; i++)
        add_spot(K_STYLE, i, (struct gfx_rect){ x0 + i * (CARD_W + GAP), y_style + lh + 10, CARD_W, CARD_H });
    for (int i = 0; i < app.naccents; i++)
        add_spot(K_ACCENT, i, (struct gfx_rect){ x0 + i * (2 * DOT_R + 20), y_accent + lh + 10,
                                                 2 * DOT_R + 8, 2 * DOT_R + 8 });
    add_spot(K_NONE_WALLPAPER, 0, (struct gfx_rect){ x0, y_background + lh + 10, THUMB_W, THUMB_H });
    for (int i = 0; i < app.nwallpapers; i++)
        add_spot(K_WALLPAPER, i, (struct gfx_rect){ x0 + (i + 1) * (THUMB_W + GAP),
                                                    y_background + lh + 10, THUMB_W, THUMB_H });
    (void)t;
}

/* A selection ring: the accent, 3 px, around r. */
static void ring(struct gfx_rect r, int radius, uint32_t colour)
{
    gfx_fill_round_rect(&canvas, r.x - 4, r.y - 4, r.w + 8, r.h + 8, radius + 4, colour);
    gfx_fill_round_rect(&canvas, r.x - 1, r.y - 1, r.w + 2, r.h + 2, radius + 1, theme_get()->window_bg);
}

/* A little window in one style: what it'll look like. */
static void style_preview(struct gfx_rect r, int light, int selected, int lit)
{
    const struct theme *t = theme_get();
    uint32_t win_bg = light ? 0xfafafb : 0x222226, head = light ? 0xebebed : 0x2e2e32;
    uint32_t card = light ? 0xffffff : 0x343437, fg = light ? 0x2e2e33 : 0xffffff;
    if (selected)
        ring(r, 10, t->accent_bg);
    else if (lit)
        ring(r, 10, gfx_mix(t->ink, t->window_bg, 60));
    gfx_fill_round_rect(&canvas, r.x, r.y, r.w, r.h, 10, win_bg);
    gfx_fill_rect(&canvas, r.x, r.y + 10, r.w, 14, head);
    gfx_fill_round_rect(&canvas, r.x, r.y, r.w, 24, 10, head);
    gfx_fill_rect(&canvas, r.x + r.w / 2 - 24, r.y + 10, 48, 4, gfx_mix(fg, head, 150));   /* title */
    gfx_fill_round_rect(&canvas, r.x + 16, r.y + 38, r.w - 32, 46, 6, card);
    for (int i = 1; i < 3; i++)
        gfx_fill_rect(&canvas, r.x + 16, r.y + 38 + i * 15, r.w - 32, 1, gfx_mix(fg, card, 30));
    for (int i = 0; i < 3; i++)
        gfx_fill_rect(&canvas, r.x + 26, r.y + 44 + i * 15, 50 - i * 10, 3, gfx_mix(fg, card, 140));
    gfx_fill_round_rect(&canvas, r.x + r.w - 66, r.y + r.h - 26, 50, 16, 4, t->accent_bg);
}

static void draw(void)
{
    const struct theme *t = theme_get();
    gfx_fill_rect(&canvas, 0, 0, canvas.w, canvas.h, t->window_bg);
    int lh = text_height(TEXT_BOLD);
    static const char *const styles[] = { "Dark", "Light" };

    text_draw(&canvas, x0, y_style, "Style", TEXT_BOLD, t->fg);
    for (int i = 0; i < nspots; i++) {
        struct spot *s = &spots[i];
        int lit = i == hover || i == pressed;
        switch (s->kind) {
        case K_STYLE:
            style_preview(s->r, s->index, app.style == s->index, lit);
            text_draw(&canvas, s->r.x + (s->r.w - text_width(styles[s->index], TEXT_BODY)) / 2,
                      s->r.y + s->r.h + 10, styles[s->index], TEXT_BODY,
                      app.style == s->index ? t->fg : t->fg_dim);
            break;
        case K_ACCENT: {
            int cx = s->r.x + s->r.w / 2, cy = s->r.y + s->r.h / 2;
            uint32_t c = app.accents[s->index].colour;
            if (app.accent == s->index) {
                gfx_fill_circle(&canvas, cx, cy, DOT_R + 4, c);
                gfx_fill_circle(&canvas, cx, cy, DOT_R + 2, t->window_bg);
            } else if (lit) {
                gfx_fill_circle(&canvas, cx, cy, DOT_R + 3, gfx_mix(t->ink, t->window_bg, 60));
                gfx_fill_circle(&canvas, cx, cy, DOT_R + 1, t->window_bg);
            }
            gfx_fill_circle(&canvas, cx, cy, DOT_R, c);
            break;
        }
        case K_NONE_WALLPAPER:
        case K_WALLPAPER: {
            int selected = s->kind == K_NONE_WALLPAPER ? !app.wallpaper[0]
                         : !strcmp(app.wallpaper, app.wallpapers[s->index]);
            if (selected)
                ring(s->r, 8, t->accent_bg);
            else if (lit)
                ring(s->r, 8, gfx_mix(t->ink, t->window_bg, 60));
            if (s->kind == K_WALLPAPER && have_thumb[s->index]) {
                struct gfx_surface thumb = { thumbs[s->index], THUMB_W, THUMB_H, THUMB_W };
                gfx_blit(&canvas, s->r.x, s->r.y, &thumb, 0, 0, THUMB_W, THUMB_H);
                gfx_round_corners(&canvas, s->r.x, s->r.y, THUMB_W, THUMB_H, 8, selected || lit
                                  ? t->window_bg : t->window_bg);
            } else {
                gfx_fill_round_rect(&canvas, s->r.x, s->r.y, THUMB_W, THUMB_H, 8,
                                    t->light ? 0xc8ccd6 : 0x202634);
            }
            const char *label = s->kind == K_NONE_WALLPAPER ? "None" : app.wallpapers[s->index];
            text_draw(&canvas, s->r.x + (THUMB_W - text_width(label, TEXT_BODY)) / 2,
                      s->r.y + THUMB_H + 8, label, TEXT_BODY, selected ? t->fg : t->fg_dim);
            break;
        }
        }
    }
    text_draw(&canvas, x0, y_accent, "Accent Color", TEXT_BOLD, t->fg);
    text_draw(&canvas, x0, y_background, "Background", TEXT_BOLD, t->fg);
    const char *note = "Changes last until LiteKern X restarts.";
    text_draw(&canvas, (canvas.w - text_width(note, TEXT_SMALL)) / 2, canvas.h - text_height(TEXT_SMALL) - 14,
              note, TEXT_SMALL, t->fg_dim);
    (void)lh;
    k86_present(0, 0, canvas.w, canvas.h);
}

static int spot_at(int x, int y)
{
    for (int i = 0; i < nspots; i++) {
        struct gfx_rect r = spots[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
            return i;
    }
    return -1;
}

static void choose(const struct spot *s)
{
    switch (s->kind) {
    case K_STYLE:
        k86_logf("settings: style %s", s->index ? "light" : "dark");
        k86_appearance_set(s->index, -1, 0);
        break;
    case K_ACCENT:
        k86_logf("settings: accent %s", app.accents[s->index].name);
        k86_appearance_set(-1, s->index, 0);
        break;
    case K_WALLPAPER:
        k86_logf("settings: background %s", app.wallpapers[s->index]);
        k86_appearance_set(-1, -1, app.wallpapers[s->index]);
        break;
    case K_NONE_WALLPAPER:
        k86_log("settings: background none");
        k86_appearance_set(-1, -1, "");
        break;
    }
    k86_appearance(&app);           /* the redraw comes with K86_EVENT_THEME */
}

static void pointer(const struct k86_event *ev)
{
    struct wg_pointer p = wg_pointer_make(ev->x, ev->y, ev->buttons, ev->changed, ev->time_ms);
    int over = spot_at(p.x, p.y), old_hover = hover, old_pressed = pressed;
    hover = over;
    if (p.down)
        pressed = over;
    if (p.up) {
        int was = pressed;
        pressed = -1;
        if (was >= 0 && was == over) {
            choose(&spots[was]);
            return;
        }
    }
    if (hover != old_hover || pressed != old_pressed)
        draw();
}

static void key(const struct key_event *k)
{
    if (!k->pressed)
        return;
    /* Left/Right walk the accents; L and D pick the style. */
    if (k->key == KEY_LEFT && app.accent > 0)
        choose(&(struct spot){ K_ACCENT, app.accent - 1, { 0, 0, 0, 0 } });
    else if (k->key == KEY_RIGHT && app.accent + 1 < app.naccents)
        choose(&(struct spot){ K_ACCENT, app.accent + 1, { 0, 0, 0, 0 } });
    else if (k->ascii == 'l' || k->ascii == 'd')
        choose(&(struct spot){ K_STYLE, k->ascii == 'l', { 0, 0, 0, 0 } });
}

int main(void)
{
    if (k86_window_open(&win))
        return 1;
    canvas = (struct gfx_surface){ win.canvas, win.w, win.h, win.stride };
    struct k86_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.title, "Appearance", 11);
    k86_header(&h);
    k86_appearance(&app);
    for (int i = 0; i < app.nwallpapers; i++)
        have_thumb[i] = k86_wallpaper_thumb(app.wallpapers[i], thumbs[i], THUMB_W, THUMB_H) == 0;
    layout();
    draw();
    k86_log("settings: open");

    for (;;) {
        struct k86_event ev;
        if (k86_wait_event(&ev))
            return 1;
        switch (ev.type) {
        case K86_EVENT_POINTER: pointer(&ev); break;
        case K86_EVENT_KEY:     key(&ev.key); break;
        case K86_EVENT_THEME:   draw(); break;
        case K86_EVENT_RESIZE:  canvas.w = ev.x; canvas.h = ev.y; layout(); draw(); break;
        case K86_EVENT_CLOSE:   return 0;
        default:                break;
        }
    }
}

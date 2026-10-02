/* LiteKern X — window system self-test (Phase 2 §3; floating windows since
 * Phase 3 §6). Test builds only (-DLKX_SELFTEST_WM); see
 * tests/kernel/test-kernel.sh.
 *
 * Checks a window's header bar and buttons pixel by pixel, and that input
 * becomes the right events (content clicks in content coordinates, header
 * buttons by id, close, keys to the focused window), then two windows:
 * focus and stacking, dragging, resizing (with RESIZE events and the
 * minimum size), maximising and minimising. Leaves a Files-style mock-up
 * open for the screenshot; the test then closes it with a real click on its
 * close button. */
#include "kernel/desktop.h"
#include "kernel/errno.h"
#include "kernel/font.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/theme.h"
#include "kernel/wm.h"

static int passed, failed;

static void check(int ok, const char *what)
{
    kprintf("selftest: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

static uint32_t px(int x, int y)
{
    struct gfx_surface *s = screen_surface();
    return s->px[y * s->stride + x];
}

/* The next event other than pointer updates (checked on their own below). */
static int next(struct wm_window *w, struct wm_event *ev)
{
    while (wm_poll_event(w, ev))
        if (ev->type != WM_EVENT_POINTER)
            return 1;
    return 0;
}

static void drain(struct wm_window *w)
{
    struct wm_event ev;
    while (wm_poll_event(w, &ev))
        ;
}

static void mouse(int x, int y, uint8_t b)
{
    wm_input_mouse(x, y, b);
    wm_present();
}

/* A Files-style list, drawn the Adwaita way: a rounded "boxed list" card
 * with separators, one selected row, a folder glyph per row. */
static void mockup(struct wm_window *w)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content(w);
    static const char *const names[] = { "Documents", "Music", "Pictures", "notes.txt",
                                         "todo.txt", "kernel.bin" };
    static const char *const sizes[] = { "3 items", "12 items", "48 items", "2 KB", "512 bytes",
                                         "28 KB" };
    int x = 40, y = 24, wd = c->w - 80, n = 6;

    gfx_fill_rect(c, 0, 0, c->w, c->h, t->window_bg);
    gfx_text(c, x, y, "Home", t->fg_dim, GFX_TRANSPARENT);
    y += FONT_H + t->spacing;
    gfx_fill_round_rect(c, x, y, wd, n * t->row_h, 12, t->view_bg);
    for (int i = 0; i < n; i++) {
        int ry = y + i * t->row_h;
        if (i == 1)
            gfx_fill_rect(c, x, ry, wd, t->row_h, t->row_selected);
        if (i)
            gfx_fill_rect(c, x + 12, ry, wd - 24, 1, t->border);
        uint32_t icon = i < 3 ? t->accent : t->fg_dim;
        gfx_fill_round_rect(c, x + 14, ry + 12, 18, 14, 3, icon);
        gfx_text(c, x + 44, ry + (t->row_h - FONT_H) / 2, names[i], t->fg, GFX_TRANSPARENT);
        gfx_text(c, x + wd - 14 - gfx_text_width(sizes[i]), ry + (t->row_h - FONT_H) / 2,
                 sizes[i], t->fg_dim, GFX_TRANSPARENT);
    }
    wm_damage(w, 0, 0, c->w, c->h);
    wm_present();
}

void selftest_wm_run(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    struct wm_event ev;
    int top = t->topbar_h, hh = t->headerbar_h;

    struct wm_window *w = wm_create("Files", 760, 480, 0, 0);
    wm_present();
    check(w && wm_focused() == w, "a window opens, focused");
    struct gfx_rect r = wm_rect(w);
    check(r.w == 760 && r.h == 480 && r.x == (s->w - 760) / 2 && r.y == top + (s->h - top - 480) / 2,
          "it opens centred below the top bar, at the size asked for");
    struct gfx_surface *c = wm_content(w);
    check(c->w == 758 && c->h == 480 - hh - 2 && c->stride >= s->w,
          "content: inside the 1 px frame, below the header bar; stride for the largest size");
    int ct = r.y + 1 + hh;                              /* first content row on screen */
    check(px(r.x + 200, r.y + 6) == t->headerbar_bg && px(r.x + 30, ct - 1) == t->headerbar_border,
          "header bar and its bottom border use the theme");
    check(px(r.x + 20, ct + 20) == t->window_bg, "content starts as the window background");
    int bright = 0;
    for (int x = r.x + r.w / 2 - 40; x < r.x + r.w / 2 + 40; x++)
        for (int y = r.y + 10; y < r.y + 36; y++)
            bright += px(x, y) == t->fg;
    check(bright > 20, "the title is drawn in the middle of the header bar");
    int cx = r.x + r.w - 24, cy = r.y + 24;            /* close button centre */
    int mx = cx - 36, nx = cx - 72;                     /* maximise, minimise */
    check(px(cx, cy - 10) == t->button_bg && px(mx, cy - 10) == t->button_bg &&
              px(nx, cy - 10) == t->button_bg,
          "minimise, maximise and close: round buttons at the right");
    check(px(r.x + 2, r.y + 1) != t->headerbar_bg, "the window's corners are rounded");

    check(wm_add_button(w, WM_LEFT, WM_ICON_BACK, 0, 1) == 0 && wm_add_button(w, WM_LEFT, WM_ICON_UP, 0, 2) == 0 &&
              wm_add_button(w, WM_RIGHT, WM_ICON_ADD, 0, 3) == 0 &&
              wm_add_button(w, WM_RIGHT, WM_ICON_NONE, "Rename", 4) == 0,
          "header buttons: icons on the left, icon and text on the right");
    int bx = r.x + 1 + t->spacing + t->button_size / 2; /* the first left button's centre */
    drain(w);

    mouse(cx, cy, 0);
    check(px(cx, cy - 10) == t->button_hover, "hovering the close button highlights it");
    mouse(cx, cy, MOUSE_LEFT);
    check(px(cx, cy - 10) == t->button_active, "pressing it shows the pressed colour");
    mouse(cx, cy, 0);
    check(next(w, &ev) && ev.type == WM_EVENT_CLOSE && !next(w, &ev), "releasing on it asks the app to close");

    mouse(bx, cy, 0);
    mouse(bx, cy, MOUSE_LEFT);
    mouse(bx, cy, 0);
    check(next(w, &ev) && ev.type == WM_EVENT_HEADER && ev.id == 1, "a header button reports its id");

    mouse(bx, cy, MOUSE_LEFT);
    mouse(bx + 200, cy + 200, MOUSE_LEFT);
    mouse(bx + 200, cy + 200, 0);
    check(!next(w, &ev), "a press dragged off its button does nothing");

    mouse(r.x + 100, ct + 50, MOUSE_LEFT);
    mouse(r.x + 100, ct + 50, 0);
    check(next(w, &ev) && ev.type == WM_EVENT_CLICK && ev.x == 99 && ev.y == 50,
          "a content click arrives in content coordinates");

    drain(w);
    mouse(r.x + 300, ct + 100, 0);
    mouse(r.x + 310, ct + 110, 0);
    mouse(r.x + 320, ct + 120, 0);
    check(wm_poll_event(w, &ev) && ev.type == WM_EVENT_POINTER && ev.x == 319 && ev.y == 120 &&
              !ev.changed && !wm_poll_event(w, &ev),
          "pointer moves reach the app, merged into the latest position");
    mouse(r.x + 320, ct + 120, MOUSE_LEFT);
    mouse(r.x + 330, ct + 120, MOUSE_LEFT);
    mouse(r.x + 330, ct + 120, 0);
    int ok_down = wm_poll_event(w, &ev) && ev.type == WM_EVENT_POINTER && ev.changed == MOUSE_LEFT &&
                  ev.buttons == MOUSE_LEFT;
    while (ok_down && wm_poll_event(w, &ev) && ev.type != WM_EVENT_POINTER)
        ;                       /* the CLICK convenience event sits in between */
    int ok_drag = ev.type == WM_EVENT_POINTER && ev.x == 329 && !ev.changed;
    int ok_up = wm_poll_event(w, &ev) && ev.type == WM_EVENT_POINTER && ev.changed == MOUSE_LEFT &&
                !ev.buttons;
    check(ok_down && ok_drag && ok_up, "press, drag and release arrive in order");

    struct key_event k = { .key = 0x1e, .pressed = 1, .ascii = 'a' };
    wm_input_key(&k);
    k.pressed = 0;
    wm_input_key(&k);
    check(next(w, &ev) && ev.type == WM_EVENT_KEY && ev.key.ascii == 'a' && !next(w, &ev),
          "keys go to the focused window (presses only)");

    int ok = 1;
    for (int i = 4; i < WM_MAX_BUTTONS; i++)
        ok &= wm_add_button(w, WM_RIGHT, WM_ICON_NONE, "x", 10 + i) == 0;
    check(ok && wm_add_button(w, WM_RIGHT, WM_ICON_NONE, "x", 99) == -ENOMEM, "button limit -> -ENOMEM");
    wm_clear_buttons(w);

    gfx_fill_rect(c, -10, -10, 50, 50, 0xff00ff);
    wm_damage(w, 0, 0, 50, 50);
    wm_present();
    check(px(r.x + 6, ct - 2) == t->headerbar_bg && px(r.x + 6, ct + 5) == 0xff00ff,
          "drawing into the content can't reach the header bar");

    /* A second window: it opens moved along, in front, and takes the keys. */
    struct wm_window *w2 = wm_create("Second", 760, 480, 0, 0);
    wm_present();
    struct gfx_rect r2 = wm_rect(w2);
    check(w2 && r2.x == r.x + 32 && r2.y == r.y + 32 && wm_focused() == w2 && wm_at(1) == w2,
          "a second window opens moved along, in front and focused");
    check(px(r2.x + 200, r2.y + 6) == t->headerbar_bg && px(r.x + 6, ct + 5) == 0xff00ff,
          "both are drawn, the new one on top");
    drain(w);
    drain(w2);
    mouse(r.x + 6, ct + 5, MOUSE_LEFT);
    mouse(r.x + 6, ct + 5, 0);
    check(wm_focused() == w && wm_at(1) == w && next(w, &ev) && ev.type == WM_EVENT_CLICK,
          "clicking the one behind brings it to the front, and the click reaches it");
    drain(w);

    /* Drag by the header bar. */
    mouse(r.x + 200, r.y + 20, MOUSE_LEFT);
    mouse(r.x + 220, r.y + 30, MOUSE_LEFT);
    mouse(r.x + 260, r.y + 60, MOUSE_LEFT);
    mouse(r.x + 260, r.y + 60, 0);
    struct gfx_rect m = wm_rect(w);
    check(m.x == r.x + 60 && m.y == r.y + 40 && m.w == r.w && m.h == r.h && !next(w, &ev),
          "dragging the header bar moves the window (and sends the app nothing)");
    check(px(m.x + 200, m.y + 6) == t->headerbar_bg, "it's drawn where it went");

    /* Resize from the bottom-right corner. */
    drain(w);
    mouse(m.x + m.w + 2, m.y + m.h + 2, MOUSE_LEFT);
    mouse(m.x + m.w - 100, m.y + m.h - 50, MOUSE_LEFT);
    mouse(m.x + m.w - 100, m.y + m.h - 50, 0);
    struct gfx_rect z = wm_rect(w);
    check(z.w == m.w - 102 && z.h == m.h - 52 && z.x == m.x && z.y == m.y, "dragging a corner resizes");
    check(next(w, &ev) && ev.type == WM_EVENT_RESIZE && ev.x == z.w - 2 && ev.y == z.h - hh - 2 &&
              c->w == ev.x && c->h == ev.y && !next(w, &ev),
          "the app hears the new size once (RESIZE), and its content follows");
    mouse(z.x + z.w - 2, z.y + z.h - 2, MOUSE_LEFT);
    mouse(z.x + 10, z.y + 10, MOUSE_LEFT);
    mouse(z.x + 10, z.y + 10, 0);
    check(wm_rect(w).w == WM_MIN_W && wm_rect(w).h == WM_MIN_H, "never smaller than the minimum");
    wm_move_resize(w, m);
    drain(w);

    /* Maximise, restore, minimise. */
    wm_set_maximised(w, 1);
    wm_present();
    struct gfx_rect a = wm_area();
    check(wm_is_maximised(w) && wm_rect(w).x == a.x && wm_rect(w).y == a.y && wm_rect(w).w == a.w &&
              wm_rect(w).h == a.h && next(w, &ev) && ev.type == WM_EVENT_RESIZE && ev.x == a.w &&
              ev.y == a.h - hh,
          "maximised: it fills the space below the top bar, square-cornered");
    check(px(a.x, a.y) == t->headerbar_bg && px(a.x + a.w - 1, a.y) == t->headerbar_bg,
          "maximised: its header bar reaches the corners");
    wm_set_maximised(w, 0);
    wm_present();
    check(!wm_is_maximised(w) && wm_rect(w).x == m.x && wm_rect(w).w == m.w && next(w, &ev) &&
              ev.type == WM_EVENT_RESIZE,
          "restored: back where and as big as it was");
    wm_set_minimised(w, 1);
    wm_present();
    check(wm_is_minimised(w) && wm_focused() == w2 && px(m.x + m.w - 4, m.y + m.h - 4) != t->window_bg,
          "minimised: gone from the screen, the other window gets the focus");
    k.pressed = 1;
    wm_input_key(&k);
    check(!next(w, &ev) && next(w2, &ev) && ev.type == WM_EVENT_KEY, "keys follow the focus");
    wm_set_minimised(w, 0);
    check(!wm_is_minimised(w) && wm_focused() == w, "restoring it brings it back in front");

    /* Clean up: the second window goes; the first, as Files will look, stays
     * centred for the screenshot. */
    wm_destroy(w2);
    check(wm_count() == 1 && wm_focused() == w, "closing a window leaves the rest");
    wm_move_resize(w, r);
    wm_set_title(w, "Files");
    wm_add_button(w, WM_LEFT, WM_ICON_BACK, 0, 1);
    wm_add_button(w, WM_LEFT, WM_ICON_UP, 0, 2);
    wm_add_button(w, WM_RIGHT, WM_ICON_ADD, 0, 3);
    wm_add_button(w, WM_RIGHT, WM_ICON_NONE, "Rename", 4);
    mockup(w);
    drain(w);

    kprintf("selftest: wm %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("window system self-test: %d checks failed", failed);
}

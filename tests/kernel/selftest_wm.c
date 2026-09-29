/* LiteKern X — window system self-test (Phase 2 §3). Test builds only
 * (-DLKX_SELFTEST_WM); see tests/kernel/test-kernel.sh.
 *
 * Checks the header bar and close button pixel by pixel, and that input
 * becomes the right events: content clicks in content coordinates, header
 * buttons by id, close, keys, and no event for a press that's dragged off
 * its button. Leaves a Files-style mock-up open for the screenshot; the
 * test then closes it with a real click on the close button. */
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

static int next(struct wm_event *ev)
{
    return wm_poll_event(ev);
}

static void drain(void)
{
    struct wm_event ev;
    while (next(&ev))
        ;
}

/* A Files-style list, drawn the Adwaita way: a rounded "boxed list" card
 * with separators, one selected row, a folder glyph per row. */
static void mockup(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    static const char *const names[] = { "Documents", "Music", "Pictures", "notes.txt",
                                         "todo.txt", "kernel.bin" };
    static const char *const sizes[] = { "3 items", "12 items", "48 items", "2 KB", "512 bytes",
                                         "28 KB" };
    int x = 160, y = 24, w = c->w - 320, n = 6;

    gfx_text(c, x, y, "Home", t->fg_dim, GFX_TRANSPARENT);
    y += FONT_H + t->spacing;
    gfx_fill_round_rect(c, x, y, w, n * t->row_h, 12, t->view_bg);
    for (int i = 0; i < n; i++) {
        int ry = y + i * t->row_h;
        if (i == 1)
            gfx_fill_rect(c, x, ry, w, t->row_h, t->row_selected);
        if (i)
            gfx_fill_rect(c, x + 12, ry, w - 24, 1, t->border);
        uint32_t icon = i < 3 ? t->accent : t->fg_dim;
        gfx_fill_round_rect(c, x + 14, ry + 12, 18, 14, 3, icon);
        gfx_text(c, x + 44, ry + (t->row_h - FONT_H) / 2, names[i], t->fg, GFX_TRANSPARENT);
        gfx_text(c, x + w - 14 - gfx_text_width(sizes[i]), ry + (t->row_h - FONT_H) / 2,
                 sizes[i], t->fg_dim, GFX_TRANSPARENT);
    }
    wm_damage(0, 0, c->w, c->h);
    wm_present();
}

void selftest_wm_run(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *s = screen_surface();
    struct wm_event ev;
    int cx = s->w - t->spacing - t->button_size / 2;       /* close button centre */
    int cy = t->headerbar_h / 2;

    wm_open("Files");
    check(wm_is_open(), "a window opens");
    check(px(s->w / 2 - 100, 4) == t->headerbar_bg && px(10, t->headerbar_h - 1) == t->headerbar_border,
          "header bar and its bottom border use the theme");
    check(px(20, t->headerbar_h + 20) == t->window_bg, "content starts as the window background");
    int white = 0;
    for (int x = s->w / 2 - 40; x < s->w / 2 + 40; x++)
        for (int y = 10; y < 36; y++)
            white += px(x, y) == t->fg;
    check(white > 20, "the title is drawn in the middle of the header bar");
    check(px(cx, cy - 10) == t->button_bg, "close button: a round button at the right");

    check(wm_add_button(WM_LEFT, WM_ICON_BACK, 0, 1) == 0 && wm_add_button(WM_LEFT, WM_ICON_UP, 0, 2) == 0 &&
              wm_add_button(WM_RIGHT, WM_ICON_ADD, 0, 3) == 0 &&
              wm_add_button(WM_RIGHT, WM_ICON_NONE, "Rename", 4) == 0,
          "header buttons: icons on the left, icon and text on the right");
    int bx = t->spacing + t->button_size / 2;            /* the first left button's centre */
    drain();

    wm_input_mouse(cx, cy, 0);
    check(px(cx, cy - 10) == t->button_hover, "hovering the close button highlights it");
    wm_input_mouse(cx, cy, MOUSE_LEFT);
    check(px(cx, cy - 10) == t->button_active, "pressing it shows the pressed colour");
    wm_input_mouse(cx, cy, 0);
    check(next(&ev) && ev.type == WM_EVENT_CLOSE && !next(&ev), "releasing on it asks the app to close");

    wm_input_mouse(bx, cy, 0);
    wm_input_mouse(bx, cy, MOUSE_LEFT);
    wm_input_mouse(bx, cy, 0);
    check(next(&ev) && ev.type == WM_EVENT_HEADER && ev.id == 1, "a header button reports its id");

    wm_input_mouse(bx, cy, MOUSE_LEFT);
    wm_input_mouse(bx + 200, cy + 200, MOUSE_LEFT);
    wm_input_mouse(bx + 200, cy + 200, 0);
    check(!next(&ev), "a press dragged off its button does nothing");

    wm_input_mouse(100, 200, MOUSE_LEFT);
    wm_input_mouse(100, 200, 0);
    check(next(&ev) && ev.type == WM_EVENT_CLICK && ev.x == 100 && ev.y == 200 - t->headerbar_h,
          "a content click arrives in content coordinates");

    struct key_event k = { .key = 0x1e, .pressed = 1, .ascii = 'a' };
    wm_input_key(&k);
    k.pressed = 0;
    wm_input_key(&k);
    check(next(&ev) && ev.type == WM_EVENT_KEY && ev.key.ascii == 'a' && !next(&ev),
          "keys go to the app (presses only)");

    int ok = 1;
    for (int i = 4; i < WM_MAX_BUTTONS; i++)
        ok &= wm_add_button(WM_RIGHT, WM_ICON_NONE, "x", 10 + i) == 0;
    check(ok && wm_add_button(WM_RIGHT, WM_ICON_NONE, "x", 99) == -ENOMEM, "button limit -> -ENOMEM");

    struct gfx_surface *c = wm_content();
    gfx_fill_rect(c, -10, -10, 50, 50, 0xff00ff);
    check(px(5, t->headerbar_h - 2) == t->headerbar_bg && px(5, t->headerbar_h + 5) == 0xff00ff,
          "drawing into the content can't reach the header bar");

    /* Leave the mock-up open, as Files will look, for the screenshot. */
    wm_clear_buttons();
    wm_add_button(WM_LEFT, WM_ICON_BACK, 0, 1);
    wm_add_button(WM_LEFT, WM_ICON_UP, 0, 2);
    wm_add_button(WM_RIGHT, WM_ICON_ADD, 0, 3);
    wm_add_button(WM_RIGHT, WM_ICON_NONE, "Rename", 4);
    gfx_fill_rect(c, 0, 0, c->w, c->h, t->window_bg);
    mockup();
    drain();

    kprintf("selftest: wm %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("window system self-test: %d checks failed", failed);
}

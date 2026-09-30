/* LiteKern X — widget self-test (Phase 2 §4). Test builds only
 * (-DLKX_SELFTEST_WIDGETS); see tests/kernel/test-kernel.sh.
 *
 * Drives each widget with synthetic input and checks its state, its result
 * and a few pixels, drawing into a surface of its own (not the screen). */
#include "kernel/font.h"
#include "kernel/printk.h"
#include "kernel/theme.h"
#include "kernel/widget.h"

#define SW 480
#define SH 360

static uint32_t buf[SW * SH];
static struct gfx_surface s = { buf, SW, SH, SW };
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
    return buf[y * SW + x];
}

/* A pointer update (valid until the next call). */
static const struct wg_pointer *at(int x, int y, int held, int down, int up, uint32_t ms)
{
    static struct wg_pointer p;
    p = (struct wg_pointer){ x, y, held, down, up, ms };
    return &p;
}

static struct key_event key(uint16_t code, uint8_t ascii)
{
    return (struct key_event){ .key = code, .pressed = 1, .ascii = ascii };
}

static void test_pointer(void)
{
    struct wg_pointer p = wg_pointer_make(5, 6, MOUSE_LEFT, MOUSE_LEFT, 77);
    check(p.held && p.down && !p.up && p.time_ms == 77, "pointer: a press is held + down");
    p = wg_pointer_make(5, 6, 0, MOUSE_LEFT, 0);
    check(!p.held && !p.down && p.up, "pointer: a release is up");
    p = wg_pointer_make(5, 6, MOUSE_RIGHT, MOUSE_RIGHT, 0);
    check(!p.held && !p.down && !p.up, "pointer: only the left button counts");
}

static void test_button(void)
{
    const struct theme *t = theme_get();
    struct wg_button b = { .r = { 20, 20, 100, 34 }, .label = "OK" };
    gfx_fill_rect(&s, 0, 0, SW, SH, t->window_bg);
    wg_button_draw(&s, &b);
    uint32_t rest = px(24, 37);
    check(rest == gfx_mix(0xffffff, t->window_bg, 26) && px(20, 20) == t->window_bg,
          "button: resting colour is white 10% over the window, corners rounded");

    check(!wg_button_pointer(&b, at(50, 30, 0, 0, 0, 0)) && b.hover && b.dirty, "button: hover");
    wg_button_draw(&s, &b);
    check(px(24, 37) != rest && !b.dirty, "button: hover is drawn lighter; drawing clears dirty");
    check(!wg_button_pointer(&b, at(50, 30, 1, 1, 0, 0)) && b.pressed, "button: press");
    check(wg_button_pointer(&b, at(50, 30, 0, 0, 1, 0)) && !b.pressed,
          "button: release over it is a click");

    wg_button_pointer(&b, at(50, 30, 1, 1, 0, 0));
    wg_button_pointer(&b, at(300, 300, 1, 0, 0, 0));
    check(!wg_button_pointer(&b, at(300, 300, 0, 0, 1, 0)), "button: release elsewhere is not");

    wg_button_set_disabled(&b, 1);
    check(!wg_button_pointer(&b, at(50, 30, 1, 1, 0, 0)) &&
              !wg_button_pointer(&b, at(50, 30, 0, 0, 1, 0)) && !b.hover,
          "button: disabled ignores the pointer");

    struct wg_button d = { .r = { 20, 80, 100, 34 }, .label = "Delete", .style = WG_BUTTON_DESTRUCTIVE };
    wg_button_draw(&s, &d);
    check(px(24, 97) == t->destructive_bg, "button: destructive is red");
}

static const char *const names[] = { "alpha", "beta", "gamma", "delta", "epsilon", "zeta",
                                     "eta", "theta", "iota", "kappa" };

static void row(void *ctx, int i, struct wg_row *out)
{
    (void)ctx;
    out->name = names[i];
    out->detail = i % 2 ? "Folder" : "1 KB";
    out->icon = i % 2 ? WG_ICON_FOLDER : WG_ICON_FILE;
}

static void test_list(void)
{
    const struct theme *t = theme_get();
    int rh = t->row_h;
    struct wg_list l = { .r = { 10, 10, 300, 4 * rh }, .count = 10, .selected = -1, .hover = -1,
                         .row = row, .last_click_row = -1 };
    gfx_fill_rect(&s, 0, 0, SW, SH, t->window_bg);
    wg_list_draw(&s, &l);
    check(wg_list_rows_shown(&l) == 4, "list: rows shown = height / row height");
    check(px(10, 10) == t->window_bg && px(160, 10 + rh / 2 - 12) != t->window_bg,
          "list: a card with rounded corners");

    check(wg_list_pointer(&l, at(100, 10 + rh + 5, 1, 1, 0, 1000)) == WG_LIST_CHANGED &&
              l.selected == 1, "list: a click selects the row under it");
    wg_list_pointer(&l, at(100, 10 + rh + 5, 0, 0, 1, 1050));
    check(wg_list_pointer(&l, at(100, 10 + rh + 5, 1, 1, 0, 1200)) == WG_LIST_ACTIVATE,
          "list: a second click soon after activates");
    check(wg_list_pointer(&l, at(100, 10 + rh + 5, 1, 1, 0, 3000)) == WG_LIST_CHANGED,
          "list: a click much later only selects");

    struct key_event down = key(KEY_DOWN, 0), end = key(KEY_END, 0), home = key(KEY_HOME, 0);
    check(wg_list_key(&l, &down) == WG_LIST_CHANGED && l.selected == 2, "list: Down moves on");
    check(wg_list_key(&l, &end) == WG_LIST_CHANGED && l.selected == 9 && l.top == 6,
          "list: End selects the last row and scrolls it into view");
    check(wg_list_key(&l, &home) == WG_LIST_CHANGED && l.selected == 0 && l.top == 0,
          "list: Home scrolls back to the top");
    struct key_event enter = key(KEY_ENTER, '\n');
    check(wg_list_key(&l, &enter) == WG_LIST_ACTIVATE, "list: Enter activates");

    wg_list_set_count(&l, 3);
    check(l.count == 3 && l.selected == 0 && l.top == 0, "list: shrinking keeps things in range");
    wg_list_select(&l, 2);
    wg_list_set_count(&l, 2);
    check(l.selected == 1, "list: the selection follows a removed last row");
}

static enum wg_entry_result type(struct wg_entry *e, const char *text)
{
    enum wg_entry_result r = WG_ENTRY_NONE;
    for (; *text; text++) {
        struct key_event k = key(0x01e, (uint8_t)*text);
        r = wg_entry_key(e, &k);
    }
    return r;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static void test_entry(void)
{
    const struct theme *t = theme_get();
    struct wg_entry e = { .r = { 10, 10, 200, 34 } };
    wg_entry_set(&e, "");
    check(type(&e, "x") == WG_ENTRY_NONE && e.len == 0, "entry: ignores keys until focused");
    e.focused = 1;
    type(&e, "notes.txt");
    check(same(e.text, "notes.txt") && e.cursor == 9, "entry: typing inserts at the cursor");

    struct key_event left = key(KEY_LEFT, 0), home = key(KEY_HOME, 0), bs = key(KEY_BACKSPACE, 8),
                     del = key(KEY_DELETE, 0), end = key(KEY_END, 0);
    for (int i = 0; i < 4; i++)
        wg_entry_key(&e, &left);
    wg_entry_key(&e, &bs);
    check(same(e.text, "note.txt") && e.cursor == 4, "entry: Left + Backspace deletes before");
    wg_entry_key(&e, &home);
    wg_entry_key(&e, &del);
    check(same(e.text, "ote.txt") && e.cursor == 0, "entry: Home + Delete deletes after");
    wg_entry_key(&e, &bs);
    check(same(e.text, "ote.txt"), "entry: Backspace at the start does nothing");
    wg_entry_key(&e, &end);
    struct key_event ctrl_a = { .key = 0x01e, .pressed = 1, .ascii = 1, .mods = MOD_CTRL };
    check(wg_entry_key(&e, &ctrl_a) == WG_ENTRY_NONE && e.len == 7, "entry: Ctrl+letters don't type");

    struct key_event enter = key(KEY_ENTER, '\n'), esc = key(KEY_ESC, 27);
    check(wg_entry_key(&e, &enter) == WG_ENTRY_ACTIVATE && wg_entry_key(&e, &esc) == WG_ENTRY_CANCEL,
          "entry: Enter activates, Esc cancels");

    wg_entry_set(&e, "");
    for (int i = 0; i < WG_TEXT_MAX + 10; i++)
        type(&e, "a");
    check(e.len == WG_TEXT_MAX - 1 && e.text[e.len] == '\0', "entry: stops at its maximum length");
    check(e.scroll > 0 && e.cursor - e.scroll <= (e.r.w - 20) / FONT_W,
          "entry: long text scrolls to keep the cursor in view");

    wg_entry_set(&e, "abcdef");
    e.focused = 0;
    check(wg_entry_pointer(&e, at(10 + 10 + 2 * FONT_W + 1, 20, 1, 1, 0, 0)) == WG_ENTRY_CHANGED &&
              e.focused && e.cursor == 2, "entry: a click focuses it and places the cursor");

    gfx_fill_rect(&s, 0, 0, SW, SH, t->window_bg);
    wg_entry_draw(&s, &e);
    check(px(10 + 5, 11) == gfx_mix(t->accent_bg, t->window_bg, 128), "entry: focused shows the ring");
}

static void test_dialog(void)
{
    const struct theme *t = theme_get();
    struct wg_dialog d;
    wg_dialog_init(&d, "New File", "Name the new file.", "Create", WG_BUTTON_SUGGESTED, 1);
    wg_dialog_layout(&d, SW, SH);
    check(d.r.x > 0 && d.r.x + d.r.w < SW && d.r.y > 0 && d.r.y + d.r.h < SH && d.entry.focused,
          "dialog: centred, entry focused");
    gfx_fill_rect(&s, 0, 0, SW, SH, t->window_bg);
    wg_dialog_show(&s, &d);
    check(px(2, 2) != t->window_bg && px(d.r.x + d.r.w / 2, d.r.y + 4) == t->dialog_bg,
          "dialog: the window behind is dimmed, the card drawn");

    type(&d.entry, "hi");
    struct key_event enter = key(KEY_ENTER, '\n'), esc = key(KEY_ESC, 27), x = key(0x02d, 'x');
    check(wg_dialog_key(&d, &x) == WG_DIALOG_NONE && same(d.entry.text, "hix"),
          "dialog: typing goes to its entry");
    check(wg_dialog_key(&d, &enter) == 1 && wg_dialog_key(&d, &esc) == 0,
          "dialog: Enter picks the action, Esc cancels");

    int cx = d.buttons[0].r.x + 5, cy = d.buttons[0].r.y + 5;
    wg_dialog_pointer(&d, at(cx, cy, 1, 1, 0, 0));
    check(wg_dialog_pointer(&d, at(cx, cy, 0, 0, 1, 0)) == 0, "dialog: clicking Cancel returns 0");

    wg_dialog_set_body(&d, "Something with that name is already here.", 1);
    wg_dialog_draw(&s, &d);
    int red = 0;
    for (int y = d.r.y; y < d.r.y + d.r.h; y++)
        for (int xx = d.r.x; xx < d.r.x + d.r.w; xx++)
            red += px(xx, y) == t->destructive;
    check(red > 20 && d.body_error, "dialog: an error shows in red");
}

void selftest_widgets_run(void)
{
    test_pointer();
    test_button();
    test_list();
    test_entry();
    test_dialog();
    kprintf("selftest: widgets %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("widget self-test: %d checks failed", failed);
}

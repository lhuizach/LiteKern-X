#include "kernel/console.h"
#include "kernel/errno.h"
#include "kernel/gfx.h"
#include "kernel/screen.h"
#include "kernel/font.h"
#include "kernel/printk.h"
#include "kernel/string.h"

#define GLYPH_W     FONT_W
#define GLYPH_H     FONT_H
#define MAX_COLS    160     /* 1280 px */
#define MAX_ROWS    64      /* 1024 px */
#define HISTORY     512     /* lines kept for scrollback */

static char history[HISTORY][MAX_COLS];     /* line n lives at history[n % HISTORY] */
static uint32_t cols, rows;
static uint32_t line, col;                  /* where the next character goes */
static uint32_t scrollback;                 /* lines scrolled up from the live view */
static uint32_t fg = 0x00c8d0dc, bg = 0x001e3a5f;
static int active;                          /* initialised: lines are recorded */
static int hidden;                          /* console_set_visible(0): record, don't draw */
static struct gfx_surface area;             /* where it draws: the screen, or the Log window */
static void (*area_damage)(int x, int y, int w, int h);    /* NULL: the screen's */
static void (*area_flush)(void);

static void damage(int x, int y, int w, int h)
{
    if (area_damage)
        area_damage(x, y, w, h);
    else
        screen_damage(x, y, w, h);
}

static void flush(void)
{
    if (area_flush)
        area_flush();
    else
        screen_present();
}

static int drawing(void)
{
    return active && !hidden;
}

static char *text(uint32_t n)
{
    return history[n % HISTORY];
}

/* First line shown on screen. */
static uint32_t top(void)
{
    uint32_t bottom_top = line + 1 > rows ? line + 1 - rows : 0;
    return bottom_top - scrollback;
}

/* Draw one cell into the screen's back buffer (presented by console_flush). */
static void cell(uint32_t c, uint32_t r, uint8_t ch, uint32_t f, uint32_t b)
{
    gfx_char(&area, (int)(c * GLYPH_W), (int)(r * GLYPH_H), (char)ch, f, b);
    damage((int)(c * GLYPH_W), (int)(r * GLYPH_H), GLYPH_W, GLYPH_H);
}

/* Scroll the live view up a line by moving the pixels already drawn, rather
 * than drawing every character again (v1's approach, and far cheaper on the
 * Atom). gfx_blit copies rows top to bottom, so moving up in place is safe. */
static void scroll_up(void)
{
    int text_h = (int)(rows * GLYPH_H);
    gfx_blit(&area, 0, 0, &area, 0, GLYPH_H, area.w, text_h - GLYPH_H);
    gfx_fill_rect(&area, 0, text_h - GLYPH_H, area.w, GLYPH_H, bg);
    damage(0, 0, area.w, text_h);
}

static void draw_indicator(void)
{
    static const char msg[] = " scrolled back - PgDn / End to return ";
    /* At the right edge of what shows (a window may be narrower than a line). */
    uint32_t shown = (uint32_t)area.w / GLYPH_W < cols ? (uint32_t)area.w / GLYPH_W : cols;
    uint32_t len = sizeof(msg) - 1, c0 = shown > len ? shown - len : 0;
    for (uint32_t i = 0; i < len && c0 + i < shown; i++)
        cell(c0 + i, 0, (uint8_t)msg[i], bg, fg);     /* inverse video */
}

/* Whole screen: drawn into the back buffer, then presented once. */
static void redraw(void)
{
    gfx_fill_rect(&area, 0, 0, area.w, area.h, bg);
    uint32_t t = top();
    for (uint32_t r = 0; r < rows && t + r <= line; r++)
        for (uint32_t c = 0; c < cols; c++)
            if (text(t + r)[c] != ' ')
                gfx_char(&area, (int)(c * GLYPH_W), (int)(r * GLYPH_H), text(t + r)[c], fg, bg);
    if (scrollback)
        draw_indicator();
    damage(0, 0, area.w, area.h);
    flush();
}

static void newline(void)
{
    col = 0;
    line++;
    memset(text(line), ' ', MAX_COLS);
    if (!drawing())
        return;
    if (scrollback) {
        scrollback = 0;         /* new output returns to the live view */
        redraw();
    } else if (line >= rows) {
        scroll_up();
    }
}

static void put(char c)
{
    switch (c) {
    case '\n':
        newline();
        return;
    case '\r':
        return;
    case '\t':
        do
            put(' ');
        while (col % 8);
        return;
    }
    if (col == cols)
        newline();
    if (drawing() && scrollback) {
        scrollback = 0;
        redraw();
    }
    text(line)[col] = c;
    if (drawing())
        cell(col, line - top(), (uint8_t)c, fg, bg);
    col++;
}

static void set_area(struct gfx_surface *s)
{
    area = *s;
    rows = (uint32_t)area.h / GLYPH_H < MAX_ROWS ? (uint32_t)area.h / GLYPH_H : MAX_ROWS;
    scrollback = 0;
}

int console_init(uint32_t font_addr)
{
    if (font_init(font_addr) < 0 || !screen_ready())
        return -ENODEV;

    struct gfx_surface *s = screen_surface();
    cols = (uint32_t)s->w / GLYPH_W < MAX_COLS ? (uint32_t)s->w / GLYPH_W : MAX_COLS;
    set_area(s);
    memset(history, ' ', sizeof(history));
    line = col = scrollback = 0;

    /* Replay the log so far into the history, then draw it all at once. */
    uint32_t len;
    const char *log = printk_log(&len);
    for (uint32_t i = 0; i < len; i++)
        put(log[i]);
    active = 1;
    redraw();
    kprintf("console: %ux%u characters, video BIOS font at 0x%05x\n", cols, rows, font_addr);
    return 0;
}

int console_active(void)
{
    return active;
}

void console_putc(char c)
{
    if (active)
        put(c);
}

void console_flush(void)
{
    if (drawing())
        flush();
}

void console_set_visible(int visible)
{
    hidden = !visible;
    if (drawing()) {
        scrollback = 0;
        redraw();
    }
}

void console_set_target(struct gfx_surface *s, void (*dmg)(int, int, int, int), void (*fl)(void))
{
    if (!active)
        return;
    if (!s || s->h < GLYPH_H) {
        s = screen_surface();
        dmg = 0;
        fl = 0;
    }
    area_damage = dmg;
    area_flush = fl;
    set_area(s);
    if (drawing())
        redraw();
}

void console_set_colours(uint32_t new_fg, uint32_t new_bg)
{
    fg = new_fg;
    bg = new_bg;
    if (drawing())
        redraw();
}

void console_scroll(int lines)
{
    if (!drawing())
        return;
    uint32_t live_top = line + 1 > rows ? line + 1 - rows : 0;
    uint32_t oldest = line >= HISTORY ? line - HISTORY + 1 : 0;
    uint32_t max = live_top > oldest ? live_top - oldest : 0;
    int64_t want = (int64_t)scrollback + lines;
    uint32_t next = want < 0 ? 0 : want > max ? max : (uint32_t)want;
    if (next != scrollback) {
        scrollback = next;
        redraw();
    }
}

uint32_t console_rows(void)
{
    return rows;
}

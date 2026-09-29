#include "kernel/console.h"
#include "kernel/errno.h"
#include "kernel/fb.h"
#include "kernel/font.h"
#include "kernel/printk.h"
#include "kernel/string.h"

#define GLYPH_W     FONT_W
#define GLYPH_H     FONT_H
#define MAX_COLS    160     /* 1280 px */
#define MAX_ROWS    64      /* 1024 px */
#define HISTORY     512     /* lines kept for scrollback */

static device_t *fb;
static char history[HISTORY][MAX_COLS];     /* line n lives at history[n % HISTORY] */
static uint32_t cols, rows;
static uint32_t line, col;                  /* where the next character goes */
static uint32_t scrollback;                 /* lines scrolled up from the live view */
static uint32_t fg = 0x00c8d0dc, bg = 0x001e3a5f;
static int active;                          /* initialised: lines are recorded */
static int hidden;                          /* console_set_visible(0): record, don't draw */

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

static void draw_glyph(uint32_t c, uint32_t r, uint8_t ch, uint32_t f, uint32_t b)
{
    uint32_t px[GLYPH_W * GLYPH_H];
    const uint8_t *glyph = font_glyph(ch);
    for (uint32_t y = 0; y < GLYPH_H; y++)
        for (uint32_t x = 0; x < GLYPH_W; x++)
            px[y * GLYPH_W + x] = (glyph[y] & (0x80 >> x)) ? f : b;
    struct fb_blit blit = { c * GLYPH_W, r * GLYPH_H, GLYPH_W, GLYPH_H, px, GLYPH_W };
    dev_ioctl(fb, FB_BLIT, &blit);
}

static void draw_indicator(void)
{
    static const char msg[] = " scrolled back - PgDn / End to return ";
    uint32_t len = sizeof(msg) - 1, c0 = cols > len ? cols - len : 0;
    for (uint32_t i = 0; i < len && c0 + i < cols; i++)
        draw_glyph(c0 + i, 0, (uint8_t)msg[i], bg, fg);     /* inverse video */
}

static void redraw(void)
{
    struct fb_info info;
    dev_ioctl(fb, FB_GET_INFO, &info);
    struct fb_rect all = { 0, 0, info.width, info.height, bg };
    dev_ioctl(fb, FB_FILL_RECT, &all);
    uint32_t t = top();
    for (uint32_t r = 0; r < rows && t + r <= line; r++)
        for (uint32_t c = 0; c < cols; c++)
            if (text(t + r)[c] != ' ')
                draw_glyph(c, r, (uint8_t)text(t + r)[c], fg, bg);
    if (scrollback)
        draw_indicator();
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
        redraw();               /* scroll */
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
        draw_glyph(col, line - top(), (uint8_t)c, fg, bg);
    col++;
}

int console_init(device_t *dev, uint32_t font_addr)
{
    struct fb_info info;
    if (font_init(font_addr) < 0 || dev_ioctl(dev, FB_GET_INFO, &info) < 0)
        return -ENODEV;

    fb = dev;
    cols = info.width / GLYPH_W < MAX_COLS ? info.width / GLYPH_W : MAX_COLS;
    rows = info.height / GLYPH_H < MAX_ROWS ? info.height / GLYPH_H : MAX_ROWS;
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

void console_set_visible(int visible)
{
    hidden = !visible;
    if (drawing()) {
        scrollback = 0;
        redraw();
    }
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

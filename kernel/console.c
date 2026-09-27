#include "kernel/console.h"
#include "kernel/errno.h"
#include "kernel/fb.h"
#include "kernel/printk.h"
#include "kernel/string.h"

#define GLYPH_W     8
#define GLYPH_H     16
#define MAX_COLS    160     /* 1280 px */
#define MAX_ROWS    64      /* 1024 px */

static device_t *fb;
static uint8_t font[256 * GLYPH_H];
static char grid[MAX_ROWS][MAX_COLS];
static uint32_t cols, rows, col, row;
static uint32_t fg = 0x00c8d0dc, bg = 0x001e3a5f;
static int active;

static void draw_cell(uint32_t c, uint32_t r)
{
    uint32_t px[GLYPH_W * GLYPH_H];
    const uint8_t *glyph = &font[(uint8_t)grid[r][c] * GLYPH_H];
    for (uint32_t y = 0; y < GLYPH_H; y++)
        for (uint32_t x = 0; x < GLYPH_W; x++)
            px[y * GLYPH_W + x] = (glyph[y] & (0x80 >> x)) ? fg : bg;
    struct fb_blit b = { c * GLYPH_W, r * GLYPH_H, GLYPH_W, GLYPH_H, px, GLYPH_W };
    dev_ioctl(fb, FB_BLIT, &b);
}

static void redraw(void)
{
    struct fb_info info;
    dev_ioctl(fb, FB_GET_INFO, &info);
    struct fb_rect all = { 0, 0, info.width, info.height, bg };
    dev_ioctl(fb, FB_FILL_RECT, &all);
    for (uint32_t r = 0; r < rows; r++)
        for (uint32_t c = 0; c < cols; c++)
            if (grid[r][c] != ' ')
                draw_cell(c, r);
}

static void newline(void)
{
    col = 0;
    if (++row < rows)
        return;
    /* Scroll: move the text up a line and redraw. */
    memmove(grid[0], grid[1], sizeof(grid[0]) * (rows - 1));
    memset(grid[rows - 1], ' ', sizeof(grid[0]));
    row = rows - 1;
    if (active)
        redraw();
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
    grid[row][col] = c;
    if (active)
        draw_cell(col, row);
    col++;
}

int console_init(device_t *dev, uint32_t font_addr)
{
    struct fb_info info;
    if (!font_addr || dev_ioctl(dev, FB_GET_INFO, &info) < 0)
        return -ENODEV;

    fb = dev;
    memcpy(font, (const void *)font_addr, sizeof(font));   /* video BIOS ROM, below 1 MiB */
    cols = info.width / GLYPH_W < MAX_COLS ? info.width / GLYPH_W : MAX_COLS;
    rows = info.height / GLYPH_H < MAX_ROWS ? info.height / GLYPH_H : MAX_ROWS;
    memset(grid, ' ', sizeof(grid));
    col = row = 0;

    /* Replay the log so far into the grid, then draw it all at once. */
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

void console_set_colours(uint32_t new_fg, uint32_t new_bg)
{
    fg = new_fg;
    bg = new_bg;
    if (active)
        redraw();
}

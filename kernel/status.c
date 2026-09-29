#include "kernel/status.h"
#include "kernel/console.h"

static const struct {
    uint32_t fg, bg;
} colours[] = {
    [STATUS_READY] = { 0x00c8d0dc, 0x001e3a5f },
    [STATUS_PANIC] = { 0x00ffffff, 0x00801010 },
};

static volatile uint8_t *fb;
static uint32_t fb_pitch, fb_width, fb_height;

void status_init(const struct boot_info *bi)
{
    if (!(bi->flags & BI_FLAG_FB) || bi->fb_bpp != 32)
        return;
    fb = (volatile uint8_t *)bi->fb_addr;
    fb_pitch = bi->fb_pitch;
    fb_width = bi->fb_width;
    fb_height = bi->fb_height;
}

void status_show(enum status s)
{
    if (console_active()) {
        if (s == STATUS_PANIC)
            console_set_visible(1);     /* a crash is never hidden behind the GUI */
        console_set_colours(colours[s].fg, colours[s].bg);
        return;
    }
    /* No console: fill the screen directly. The framebuffer is identity-mapped
     * both before and after paging is enabled. */
    if (!fb)
        return;
    for (uint32_t y = 0; y < fb_height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(fb + y * fb_pitch);
        for (uint32_t x = 0; x < fb_width; x++)
            row[x] = colours[s].bg;
    }
}

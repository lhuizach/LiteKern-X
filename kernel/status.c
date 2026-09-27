#include "kernel/status.h"

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
    static const uint32_t colour[] = {
        [STATUS_READY] = 0x001e3a5f,
        [STATUS_PANIC] = 0x00801010,
    };
    if (!fb)
        return;
    for (uint32_t y = 0; y < fb_height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(fb + y * fb_pitch);
        for (uint32_t x = 0; x < fb_width; x++)
            row[x] = colour[s];
    }
}

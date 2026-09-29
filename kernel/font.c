#include "kernel/font.h"
#include "kernel/errno.h"
#include "kernel/string.h"

static uint8_t glyphs[256 * FONT_H];
static int loaded;

int font_init(uint32_t font_addr)
{
    if (!font_addr)
        return -ENODEV;
    memcpy(glyphs, (const void *)font_addr, sizeof(glyphs));   /* video BIOS ROM, below 1 MiB */
    loaded = 1;
    return 0;
}

int font_available(void)
{
    return loaded;
}

const uint8_t *font_glyph(uint8_t c)
{
    return &glyphs[c * FONT_H];
}

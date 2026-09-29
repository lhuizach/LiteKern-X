/* LiteKern X — the 8x16 bitmap font: the video BIOS's own, located by stage 2
 * (boot_info.font_addr) and copied here once. Used by the on-screen console
 * and the renderer (kernel/gfx.h). A proportional anti-aliased font like v1's
 * is Phase 3 polish. */
#ifndef LKX_FONT_H
#define LKX_FONT_H

#include <stdint.h>

#define FONT_W 8
#define FONT_H 16

/* 0, or -ENODEV if stage 2 found no font. */
int font_init(uint32_t font_addr);
int font_available(void);

/* 16 rows, bit 7 = leftmost pixel. */
const uint8_t *font_glyph(uint8_t c);

#endif

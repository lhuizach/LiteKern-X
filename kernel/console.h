/* LiteKern X — on-screen log console (Phase 1 §6).
 *
 * A debug console, not GUI: it shows the kernel log on the framebuffer so the
 * EeePC (no serial port) can be debugged. It draws only through the display
 * driver (fb0 ioctls), using the video BIOS 8x16 font stage 2 located. On
 * init it replays everything logged since boot. The background colour
 * carries the boot status (kernel/status.h). */
#ifndef LKX_CONSOLE_H
#define LKX_CONSOLE_H

#include <stdint.h>
#include "kernel/driver.h"

/* 0, or -ENODEV if there is no font or the display won't answer. */
int console_init(device_t *fb, uint32_t font_addr);

int console_active(void);
void console_putc(char c);

/* Change colours and redraw everything (used for the ready/panic status). */
void console_set_colours(uint32_t fg, uint32_t bg);

#endif

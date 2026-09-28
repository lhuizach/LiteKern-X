/* LiteKern X — on-screen log console (Phase 1 §6).
 *
 * A debug console, not GUI: it shows the kernel log on the framebuffer so the
 * EeePC (no serial port) can be debugged. It draws only through the display
 * driver (fb0 ioctls), using the video BIOS 8x16 font stage 2 located. On
 * init it replays everything logged since boot, and keeps 512 lines of
 * scrollback (the idle loop maps PgUp/PgDn/Home/End to console_scroll()).
 * The background colour carries the boot status (kernel/status.h). */
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

/* Scrollback: move the view `lines` up (> 0) or down (< 0) through the last
 * 512 lines, clamped. Any new output returns to the live view. */
void console_scroll(int lines);
uint32_t console_rows(void);

#endif

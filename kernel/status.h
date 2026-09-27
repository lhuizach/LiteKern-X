/* LiteKern X — boot status shown on screen.
 *
 * Once the on-screen console is up (kernel/console.h), the status is its
 * background colour behind the log. Before that — or if there is no console
 * (no BIOS font) — it is a full-screen fill straight to the framebuffer, the
 * only sign of life the EeePC (no serial port) can give that early. */
#ifndef LKX_STATUS_H
#define LKX_STATUS_H

#include "boot/bootinfo.h"

enum status {
    STATUS_READY,   /* navy: kernel reached "ready" */
    STATUS_PANIC,   /* dark red: panic() */
};

void status_init(const struct boot_info *bi);
void status_show(enum status s);

#endif

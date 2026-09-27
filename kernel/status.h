/* LiteKern X — boot status as a full-screen colour.
 *
 * The EeePC has no serial port and there is no text console until the display
 * driver exists (Phase 1 §6), so on real hardware this colour is the only
 * sign of how boot went. Not a display driver: one fill, no fonts, no state
 * beyond the framebuffer info stage 2 handed over. */
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

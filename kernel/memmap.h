/* LiteKern X — BIOS memory map and display mode, as handed over in boot_info
 * (Phase 1 §3). Only reads and reports: nothing allocates memory yet. */
#ifndef LKX_MEMMAP_H
#define LKX_MEMMAP_H

#include "boot/bootinfo.h"

/* Log the E820 map and the framebuffer mode; panic if there is no usable
 * RAM above 1 MiB. Returns usable RAM in KiB. */
uint32_t memmap_report(const struct boot_info *bi);

#endif

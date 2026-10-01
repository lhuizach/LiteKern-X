/* LiteKern X — the ramdisk (Phase 2 §5, moved onto the disk in Phase 3): a
 * read-only archive made by tools/mkramdisk.py holding the apps
 * (apps/<name>/kerns.json and its .lkx) and the wallpapers
 * (wallpapers/<name>.lkxw).
 *
 * It sits on the boot disk at sector RAMDISK_LBA, after the kernel, and is
 * read whole into memory at boot through boot0 (the BIOS). It used to be
 * built into the kernel image, but the kernel must stay under stage 2's
 * 448 KiB limit, and the wallpapers alone are bigger than that. */
#ifndef LKX_RAMDISK_H
#define LKX_RAMDISK_H

#include <stdint.h>

/* Read it from the boot disk (after the drivers are bound). Logs the result. */
int ramdisk_init(void);

int ramdisk_count(void);
/* The i-th file's name, or NULL. */
const char *ramdisk_name(int i);
/* Find a file by its full name. 0, or -ENOENT. */
int ramdisk_find(const char *name, const void **data, uint32_t *size);

#endif

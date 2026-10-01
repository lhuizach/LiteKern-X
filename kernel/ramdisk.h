/* LiteKern X — the ramdisk (Phase 2 §5, moved onto the disk in Phase 3): a
 * read-only archive made by tools/mkramdisk.py holding the apps
 * (apps/<name>/kerns.json and its .lkx) and the wallpapers
 * (wallpapers/<name>.lkxw).
 *
 * It sits on the boot disk at sector RAMDISK_LBA, after the kernel. At boot
 * only its index is read (through boot0, the BIOS); each file is read the
 * first time it's asked for and kept, so boot time doesn't grow with the
 * number of apps and wallpapers. It used to be built into the kernel image,
 * but the kernel must stay under stage 2's 448 KiB limit. */
#ifndef LKX_RAMDISK_H
#define LKX_RAMDISK_H

#include <stdint.h>

/* Read its index from the boot disk (after the drivers are bound). Logs it. */
int ramdisk_init(void);

int ramdisk_count(void);
/* The i-th file's name, or NULL. */
const char *ramdisk_name(int i);
/* Find a file by its full name, reading it in if it isn't yet. 0, -ENOENT,
 * or the read's error. */
int ramdisk_find(const char *name, const void **data, uint32_t *size);
/* Its size, without reading it. */
int ramdisk_size(const char *name, uint32_t *size);

#endif

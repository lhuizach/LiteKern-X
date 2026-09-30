/* LiteKern X — the ramdisk (Phase 2 §5): a read-only archive built into the
 * kernel image by tools/mkramdisk.py, holding the apps (apps/<name>/kerns.json
 * and its .lkx). */
#ifndef LKX_RAMDISK_H
#define LKX_RAMDISK_H

#include <stdint.h>

int ramdisk_count(void);
/* The i-th file's name, or NULL. */
const char *ramdisk_name(int i);
/* Find a file by its full name. 0, or -ENOENT. */
int ramdisk_find(const char *name, const void **data, uint32_t *size);

#endif

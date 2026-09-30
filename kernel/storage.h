/* LiteKern X — the disks Files can open (Phase 2 §5a).
 *
 * At boot every block device's MBR is read; each FAT32 partition on it
 * (type 0x0b or 0x0c) becomes a volume, mounted with kernel/fat32.h. Other
 * partitions are listed as unsupported and never touched (docs/NON-GOALS.md:
 * only FAT32 is read or written). */
#ifndef LKX_STORAGE_H
#define LKX_STORAGE_H

#include "kernel/fat32.h"

#define STORAGE_MAX 8

struct storage {
    char name[40];              /* e.g. "USB stick (LITEKERNX)" */
    int supported;              /* a mounted FAT32 volume */
    struct fat_volume fat;
    const char *why_not;        /* when not supported, e.g. "NTFS" */
};

/* Scan the block devices (after the drivers are bound). Logs what it finds. */
void storage_init(void);

int storage_count(void);
struct storage *storage_get(int i);

#endif

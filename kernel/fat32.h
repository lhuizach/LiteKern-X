/* LiteKern X — FAT32 (Phase 2 §5a): list, read, create, rename and delete
 * files and folders, with long names (VFAT), the way Windows and Linux
 * store them.
 *
 * Only FAT32 (docs/NON-GOALS.md: other filesystems, and writing anything
 * that isn't FAT32, stay out). The code knows nothing about the kernel: it
 * reads and writes sectors through the callbacks in struct fat_volume, so
 * tests/fat/ also builds it on Linux and checks everything it writes with
 * fsck.fat and mtools.
 *
 * Folders are named by their first cluster (FAT32's root folder has one
 * too: fat_root()). Names are ASCII; other characters read as '?'. Name
 * comparisons ignore case, as FAT's do. */
#ifndef LKX_FAT32_H
#define LKX_FAT32_H

#include <stdint.h>

#define FAT_NAME_MAX 64         /* longest name we create or show, NUL included */

struct fat_volume {
    /* Set by the caller before fat_mount(). LBAs are the device's. */
    int (*read)(void *ctx, uint32_t lba, uint32_t count, void *buf);
    int (*write)(void *ctx, uint32_t lba, uint32_t count, const void *buf);
    /* The current date and time in FAT's format (may be NULL: 2026-01-01). */
    void (*now)(void *ctx, uint16_t *date, uint16_t *time);
    void *ctx;
    uint32_t part_lba;          /* where the partition starts */
    int read_only;

    /* Filled in by fat_mount(). */
    uint32_t sec_per_clus, reserved, nfats, fat_size, root_clus, total_sectors;
    uint32_t data_start;        /* first data sector, from part_lba */
    uint32_t clusters;          /* data clusters: 2 .. clusters + 1 */
    uint32_t fsinfo;            /* FSInfo sector (0: none) */
    uint32_t free_count, next_free;     /* from FSInfo, kept up to date */
    char label[12];
};

struct fat_entry {
    char name[FAT_NAME_MAX];    /* longer names are cut short, marked with '~' */
    uint32_t size;
    uint32_t cluster;           /* first cluster (0: an empty file) */
    int is_dir;
    uint16_t date, time;        /* last modified */
    /* Where its directory entries are, for rename and delete. */
    uint32_t dir;               /* the folder's first cluster */
    uint32_t slot;              /* index of the short entry in the folder */
    uint32_t lfn_slots;         /* long-name entries just before it */
};

/* Read the boot sector and FSInfo. 0, or -EINVAL if it isn't FAT32. */
int fat_mount(struct fat_volume *v);
uint32_t fat_root(const struct fat_volume *v);

/* Call fn for each entry of a folder (not "." / ".." or the volume label).
 * fn returns nonzero to stop. Returns the number of entries seen, or a
 * negative error. */
int fat_list(struct fat_volume *v, uint32_t dir,
             int (*fn)(void *ctx, const struct fat_entry *e), void *ctx);

/* Find a name in a folder (ignoring case). 0 or -ENOENT. */
int fat_find(struct fat_volume *v, uint32_t dir, const char *name, struct fat_entry *out);

/* Read from a file. Returns the bytes read (0 at the end) or an error. */
int fat_read(struct fat_volume *v, const struct fat_entry *e, uint32_t offset, void *buf,
             uint32_t len);

/* Create an empty file, or a folder, in dir. -EEXIST if the name is taken,
 * -EINVAL for a name FAT can't hold, -ENOSPC if the disk or folder is full,
 * -EROFS on a read-only volume. */
int fat_create(struct fat_volume *v, uint32_t dir, const char *name, int is_dir,
               struct fat_entry *out);

/* Rename within the same folder (a change of case alone is allowed). */
int fat_rename(struct fat_volume *v, struct fat_entry *e, const char *new_name);

/* Delete a file, or a folder with everything in it. */
int fat_delete(struct fat_volume *v, const struct fat_entry *e);

/* Free space, in bytes (from FSInfo, or counted). */
uint64_t fat_free_bytes(struct fat_volume *v);

/* NULL if a name is fine to create, else a short reason for the user. */
const char *fat_bad_name(const char *name);

#endif

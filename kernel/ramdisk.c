#include "kernel/ramdisk.h"
#include "kernel/block.h"
#include "kernel/driver.h"
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/string.h"
#include "kernel/timing.h"
#include "defaults.h"           /* RAMDISK_LBA */

#define NAME_MAX 56
#define MAX_FILES 256
#define FILE_MAX (4u * 1024 * 1024)

struct __attribute__((packed)) entry {
    char name[NAME_MAX];
    uint32_t offset, size;
};

/* The table, read at boot; each file's data, read the first time it's asked
 * for and kept (the ramdisk is read-only, so a copy never goes stale). */
static struct entry table[MAX_FILES];
static const uint8_t *loaded[MAX_FILES];
static int count;
static device_t *disk;

static int read_sectors(uint32_t lba, uint32_t n, void *buf)
{
    struct blk_io io = { lba, n, buf };
    return dev_ioctl(disk, BLK_READ, &io);
}

int ramdisk_init(void)
{
    static uint8_t first[BLK_SECTOR];
    uint32_t t0 = uptime_ms();
    disk = device_find("boot0");
    if (!disk || disk->state != DEVICE_BOUND || read_sectors(RAMDISK_LBA, 1, first)) {
        kprintf("ramdisk: can't read the boot disk; no apps or wallpapers\n");
        disk = 0;
        return -ENODEV;
    }
    uint32_t n;
    memcpy(&n, first + 4, 4);
    if (memcmp(first, "LKXR", 4) || n > MAX_FILES) {
        kprintf("ramdisk: not found at sector %u\n", RAMDISK_LBA);
        disk = 0;
        return -ENOENT;
    }
    /* The table may span sectors. */
    uint32_t bytes = 8 + n * sizeof(struct entry);
    uint32_t sectors = (bytes + BLK_SECTOR - 1) / BLK_SECTOR;
    uint32_t phys = pmm_alloc_contiguous((sectors * BLK_SECTOR + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!phys || read_sectors(RAMDISK_LBA, sectors, (void *)phys)) {
        disk = 0;
        return -ENOMEM;
    }
    memcpy(table, (const uint8_t *)phys + 8, n * sizeof(struct entry));
    for (uint32_t off = 0; off < sectors * BLK_SECTOR; off += PAGE_SIZE)
        pmm_free(phys + off);
    count = (int)n;
    uint32_t total = 0;
    for (int i = 0; i < count; i++) {
        table[i].name[NAME_MAX - 1] = '\0';
        if (table[i].size > FILE_MAX)
            table[i].size = 0;              /* nonsense: treat as empty, never read it */
        total += table[i].size;
    }
    kprintf("ramdisk: %d files, %u KB, index read in %u ms\n", count, total / 1024, uptime_ms() - t0);
    return 0;
}

int ramdisk_count(void)
{
    return count;
}

const char *ramdisk_name(int i)
{
    return i >= 0 && i < count ? table[i].name : 0;
}

/* Read file i into memory (once). */
static int load(int i)
{
    if (loaded[i])
        return 0;
    if (!disk)
        return -ENODEV;
    uint32_t first = table[i].offset / BLK_SECTOR, skip = table[i].offset % BLK_SECTOR;
    uint32_t sectors = (skip + table[i].size + BLK_SECTOR - 1) / BLK_SECTOR;
    if (!sectors)
        sectors = 1;
    uint32_t phys = pmm_alloc_contiguous((sectors * BLK_SECTOR + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!phys)
        return -ENOMEM;
    int err = read_sectors(RAMDISK_LBA + first, sectors, (void *)phys);
    if (err) {
        for (uint32_t off = 0; off < sectors * BLK_SECTOR; off += PAGE_SIZE)
            pmm_free(phys + off);
        kprintf("ramdisk: reading %s failed (%s)\n", table[i].name, errno_name(err));
        return err;
    }
    loaded[i] = (const uint8_t *)phys + skip;
    return 0;
}

int ramdisk_find(const char *name, const void **data, uint32_t *size)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(table[i].name, name)) {
            int err = load(i);
            if (err)
                return err;
            *data = loaded[i];
            *size = table[i].size;
            return 0;
        }
    return -ENOENT;
}

int ramdisk_size(const char *name, uint32_t *size)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(table[i].name, name)) {
            *size = table[i].size;
            return 0;
        }
    return -ENOENT;
}

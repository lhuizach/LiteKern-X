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
#define RAMDISK_MAX (8u * 1024 * 1024)

struct __attribute__((packed)) entry {
    char name[NAME_MAX];
    uint32_t offset, size;
};

static const uint8_t *data;     /* the whole ramdisk, once read */
static uint32_t data_size;

static int read_sectors(device_t *d, uint32_t lba, uint32_t count, void *buf)
{
    struct blk_io io = { lba, count, buf };
    return dev_ioctl(d, BLK_READ, &io);
}

int ramdisk_init(void)
{
    static uint8_t first[BLK_SECTOR];
    uint32_t t0 = uptime_ms();
    device_t *d = device_find("boot0");
    if (!d || d->state != DEVICE_BOUND || read_sectors(d, RAMDISK_LBA, 1, first)) {
        kprintf("ramdisk: can't read the boot disk; no apps or wallpapers\n");
        return -ENODEV;
    }
    uint32_t count;
    memcpy(&count, first + 4, 4);
    if (memcmp(first, "LKXR", 4) || count > 1024) {
        kprintf("ramdisk: not found at sector %u\n", RAMDISK_LBA);
        return -ENOENT;
    }
    /* The table may span sectors: read it, then size the whole thing. */
    uint32_t table_bytes = 8 + count * sizeof(struct entry);
    uint32_t table_sectors = (table_bytes + BLK_SECTOR - 1) / BLK_SECTOR;
    uint32_t phys = pmm_alloc_contiguous((table_sectors * BLK_SECTOR + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!phys || read_sectors(d, RAMDISK_LBA, table_sectors, (void *)phys))
        return -ENOMEM;
    uint32_t total = table_bytes;
    const struct entry *e = (const struct entry *)(phys + 8);
    for (uint32_t i = 0; i < count; i++)
        if (e[i].offset + e[i].size > total && e[i].offset + e[i].size >= e[i].offset)
            total = e[i].offset + e[i].size;
    for (uint32_t off = 0; off < table_sectors * BLK_SECTOR; off += PAGE_SIZE)
        pmm_free(phys + off);
    if (total > RAMDISK_MAX) {
        kprintf("ramdisk: claims %u KB, more than the %u KB allowed\n", total / 1024,
                RAMDISK_MAX / 1024);
        return -EINVAL;
    }

    uint32_t sectors = (total + BLK_SECTOR - 1) / BLK_SECTOR;
    uint32_t buf = pmm_alloc_contiguous((sectors * BLK_SECTOR + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!buf)
        return -ENOMEM;
    int err = read_sectors(d, RAMDISK_LBA, sectors, (void *)buf);
    if (err) {
        kprintf("ramdisk: read failed (%s)\n", errno_name(err));
        return err;
    }
    data = (const uint8_t *)buf;
    data_size = total;
    kprintf("ramdisk: %u files, %u KB, read in %u ms\n", count, total / 1024, uptime_ms() - t0);
    return 0;
}

static const struct entry *table(uint32_t *count)
{
    *count = 0;
    if (!data)
        return 0;
    uint32_t n;
    memcpy(&n, data + 4, 4);
    if (n > (data_size - 8) / sizeof(struct entry))
        return 0;
    *count = n;
    return (const struct entry *)(data + 8);
}

/* An entry is usable if its name ends inside the field and its data inside
 * the ramdisk. */
static int valid(const struct entry *e)
{
    int terminated = 0;
    for (int i = 0; i < NAME_MAX && !terminated; i++)
        terminated = e->name[i] == '\0';
    return terminated && e->offset <= data_size && e->size <= data_size - e->offset;
}

int ramdisk_count(void)
{
    uint32_t n;
    table(&n);
    return (int)n;
}

const char *ramdisk_name(int i)
{
    uint32_t n;
    const struct entry *t = table(&n);
    if (i < 0 || (uint32_t)i >= n || !valid(&t[i]))
        return 0;
    return t[i].name;
}

int ramdisk_find(const char *name, const void **out, uint32_t *size)
{
    uint32_t n;
    const struct entry *t = table(&n);
    for (uint32_t i = 0; i < n; i++)
        if (valid(&t[i]) && !strcmp(t[i].name, name)) {
            *out = data + t[i].offset;
            *size = t[i].size;
            return 0;
        }
    return -ENOENT;
}

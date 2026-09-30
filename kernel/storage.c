#include "kernel/storage.h"
#include "kernel/block.h"
#include "kernel/driver.h"
#include "kernel/errno.h"
#include "kernel/printk.h"
#include "kernel/rtc.h"
#include "kernel/string.h"

static struct storage volumes[STORAGE_MAX];
static int count;

static int blk_read(void *ctx, uint32_t lba, uint32_t n, void *buf)
{
    struct blk_io io = { lba, n, buf };
    return dev_ioctl(ctx, BLK_READ, &io);
}

static int blk_write(void *ctx, uint32_t lba, uint32_t n, const void *buf)
{
    struct blk_io io = { lba, n, (void *)buf };
    return dev_ioctl(ctx, BLK_WRITE, &io);
}

/* The RTC's time, in FAT's format. */
static void now(void *ctx, uint16_t *date, uint16_t *time)
{
    (void)ctx;
    device_t *rtc = device_find("rtc0");
    struct rtc_time t;
    if (!rtc || rtc->state != DEVICE_BOUND || dev_read(rtc, &t, sizeof(t)) < 0 || t.year < 1980)
        return;
    *date = (uint16_t)((t.year - 1980) << 9 | t.month << 5 | t.day);
    *time = (uint16_t)(t.hour << 11 | t.minute << 5 | t.second / 2);
}

static void copy(char *dst, const char *src, int max)
{
    int i = 0;
    for (; src[i] && i < max - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static const char *type_name(uint8_t type)
{
    switch (type) {
    case 0x07: return "NTFS or exFAT";
    case 0x0b:
    case 0x0c: return "FAT32";
    case 0x01:
    case 0x04:
    case 0x06:
    case 0x0e: return "FAT12/16";
    case 0x7f: return "LiteKern X boot";
    case 0x83: return "Linux";
    case 0xee: return "GPT";
    default:   return "unknown";
    }
}

static void add(device_t *dev, const struct blk_info *info, uint32_t lba, uint8_t type)
{
    if (count == STORAGE_MAX)
        return;
    struct storage *s = &volumes[count];
    memset(s, 0, sizeof(*s));
    int fat32 = type == 0x0b || type == 0x0c;
    if (!fat32) {
        s->why_not = type_name(type);
    } else {
        s->fat = (struct fat_volume){ .read = blk_read, .write = info->read_only ? 0 : blk_write,
                                      .now = now, .ctx = dev, .part_lba = lba };
        int err = fat_mount(&s->fat);
        if (err) {
            kprintf("storage: %s partition at %u: not a FAT32 filesystem (%s)\n", dev->name, lba,
                    errno_name(err));
            s->why_not = "damaged FAT32";
        } else {
            s->supported = 1;
        }
    }
    /* "USB stick (LITEKERNX)": the BIOS drive is what we booted from. */
    const char *what = !strcmp(dev->name, "boot0") ? "Boot disk" : "Internal disk";
    int n = 0;
    for (; what[n]; n++)
        s->name[n] = what[n];
    s->name[n] = '\0';
    if (s->supported && s->fat.label[0] && strcmp(s->fat.label, "NO NAME")) {
        copy(s->name + n, " (", (int)sizeof(s->name) - n);
        copy(s->name + n + 2, s->fat.label, (int)sizeof(s->name) - n - 2);
        n = (int)strlen(s->name);
        copy(s->name + n, ")", (int)sizeof(s->name) - n);
    }
    kprintf("storage: %s partition at %u (%s): %s\n", dev->name, lba, type_name(type),
            s->supported ? (s->fat.read_only ? "FAT32, read-only" : "FAT32, read/write")
                         : "not supported, left alone");
    count++;
}

static void scan(device_t *dev)
{
    struct blk_info info;
    static uint8_t mbr[BLK_SECTOR];
    if (dev_ioctl(dev, BLK_GET_INFO, &info) < 0 || blk_read(dev, 0, 1, mbr))
        return;
    if (mbr[510] != 0x55 || mbr[511] != 0xaa) {
        kprintf("storage: %s has no partition table\n", dev->name);
        return;
    }
    for (int i = 0; i < 4; i++) {
        const uint8_t *p = mbr + 446 + i * 16;
        uint8_t type = p[4];
        uint32_t lba = p[8] | p[9] << 8 | p[10] << 16 | (uint32_t)p[11] << 24;
        uint32_t size = p[12] | p[13] << 8 | p[14] << 16 | (uint32_t)p[15] << 24;
        if (!type || !size || type == 0x7f)
            continue;                       /* empty, or our own boot area */
        if (info.sectors && (lba >= info.sectors || size > info.sectors - lba))
            continue;                       /* past the end of the disk: ignore it */
        add(dev, &info, lba, type);
    }
}

void storage_init(void)
{
    static const char *const names[] = { "boot0", "ata0" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        device_t *d = device_find(names[i]);
        if (d && d->state == DEVICE_BOUND)
            scan(d);
    }
    if (!count)
        kprintf("storage: no partitions found\n");
}

int storage_count(void)
{
    return count;
}

struct storage *storage_get(int i)
{
    return i >= 0 && i < count ? &volumes[i] : 0;
}

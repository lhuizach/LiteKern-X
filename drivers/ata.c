/* LiteKern X — the internal disk, read-only (Phase 2 §5a). Legacy device
 * "ata0": ATA PIO on the IDE controller (the EeePC's ICH7 SATA in IDE mode,
 * 8086:27c4; QEMU's and VirtualBox's PIIX IDE). Interface: kernel/block.h.
 *
 * Read-only on purpose (docs/NON-GOALS.md): the internal disk usually holds
 * Windows, and nothing here writes to it. BLK_WRITE is -EROFS.
 *
 * On init it looks at the four drive positions (primary/secondary, master/
 * slave) and takes the first ATA disk that isn't the one we booted from: in
 * the VMs the boot image is itself an IDE disk, so a drive whose first
 * sector matches boot0's is skipped. No IRQs (nIEN set): every command is
 * polled, with bounded waits. */
#include "drivers/builtin.h"
#include "kernel/block.h"
#include "kernel/errno.h"
#include "kernel/io.h"
#include "kernel/pci.h"
#include "kernel/printk.h"
#include "kernel/string.h"

#define REG_DATA    0
#define REG_ERROR   1
#define REG_COUNT   2
#define REG_LBA0    3
#define REG_LBA1    4
#define REG_LBA2    5
#define REG_DRIVE   6
#define REG_CMD     7       /* write: command; read: status */
#define CTL_NIEN    0x02    /* device control: no interrupts */

#define ST_ERR      0x01
#define ST_DRQ      0x08
#define ST_DF       0x20
#define ST_BSY      0x80

#define CMD_READ        0x20
#define CMD_READ_EXT    0x24
#define CMD_IDENTIFY    0xec

#define WAIT_LOOPS  2000000     /* status reads (~1 us each): about 2 s */

struct disk {
    uint16_t io, ctl;
    int slave;
    uint32_t sectors;
    int lba48;
};

static struct disk disk;
static struct blk_info info;

static void read_words(uint16_t port, uint16_t *buf, int n)
{
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(n) : "d"(port) : "memory");
}

/* The 400 ns a drive needs after being selected: four alternate status reads. */
static void settle(const struct disk *d)
{
    for (int i = 0; i < 4; i++)
        inb(d->ctl);
}

static int wait_ready(const struct disk *d, int want_drq)
{
    for (int i = 0; i < WAIT_LOOPS; i++) {
        uint8_t st = inb(d->io + REG_CMD);
        if (st == 0xff)
            return -ENODEV;                 /* floating bus: nothing there */
        if (st & ST_BSY)
            continue;
        if (st & (ST_ERR | ST_DF))
            return -EIO;
        if (!want_drq || (st & ST_DRQ))
            return 0;
    }
    return -EIO;                            /* timed out */
}

static int identify(struct disk *d, uint16_t id[256])
{
    outb(d->ctl, CTL_NIEN);
    outb(d->io + REG_DRIVE, (uint8_t)(0xa0 | d->slave << 4));
    settle(d);
    for (int r = REG_COUNT; r <= REG_LBA2; r++)
        outb((uint16_t)(d->io + r), 0);
    outb(d->io + REG_CMD, CMD_IDENTIFY);
    uint8_t st = inb(d->io + REG_CMD);
    if (st == 0 || st == 0xff)
        return -ENODEV;                     /* no drive */
    for (int i = 0; i < WAIT_LOOPS && (inb(d->io + REG_CMD) & ST_BSY); i++)
        ;
    if (inb(d->io + REG_LBA1) || inb(d->io + REG_LBA2))
        return -ENODEV;                     /* ATAPI (a CD drive): not a disk */
    int err = wait_ready(d, 1);
    if (err)
        return err;
    read_words(d->io + REG_DATA, id, 256);
    return 0;
}

static int read_sectors(const struct disk *d, uint32_t lba, uint32_t count, uint8_t *buf)
{
    int ext = lba + count > 0x0fffffff;
    if (ext && !d->lba48)
        return -EINVAL;
    int err = wait_ready(d, 0);
    if (err)
        return err;
    if (ext) {
        outb(d->io + REG_DRIVE, (uint8_t)(0x40 | d->slave << 4));
        settle(d);
        outb(d->io + REG_COUNT, (uint8_t)(count >> 8));
        outb(d->io + REG_LBA0, (uint8_t)(lba >> 24));
        outb(d->io + REG_LBA1, 0);
        outb(d->io + REG_LBA2, 0);
    } else {
        outb(d->io + REG_DRIVE, (uint8_t)(0xe0 | d->slave << 4 | (lba >> 24 & 0x0f)));
        settle(d);
    }
    outb(d->io + REG_COUNT, (uint8_t)count);            /* 0 means 256 */
    outb(d->io + REG_LBA0, (uint8_t)lba);
    outb(d->io + REG_LBA1, (uint8_t)(lba >> 8));
    outb(d->io + REG_LBA2, (uint8_t)(lba >> 16));
    outb(d->io + REG_CMD, ext ? CMD_READ_EXT : CMD_READ);
    for (uint32_t i = 0; i < count; i++) {
        settle(d);
        if ((err = wait_ready(d, 1)))
            return err;
        read_words(d->io + REG_DATA, (uint16_t *)(buf + i * BLK_SECTOR), BLK_SECTOR / 2);
    }
    return 0;
}

/* The controller's ports: legacy, unless its programming interface says a
 * channel runs in native mode (then they're in its BARs). */
static void channel_ports(int ch, uint16_t *io, uint16_t *ctl)
{
    *io = ch ? 0x170 : 0x1f0;
    *ctl = ch ? 0x376 : 0x3f6;
    for (uint32_t i = 0; i < pci_device_count; i++) {
        struct pci_device *p = &pci_devices[i];
        if (p->class_code != 0x01 || p->subclass != 0x01)
            continue;
        if (p->prog_if & (ch ? 0x04 : 0x01)) {
            uint32_t bar_io = pci_read32(p->bus, p->dev, p->func, (uint8_t)(0x10 + ch * 8));
            uint32_t bar_ctl = pci_read32(p->bus, p->dev, p->func, (uint8_t)(0x14 + ch * 8));
            if ((bar_io & 1) && (bar_ctl & 1)) {
                *io = (uint16_t)(bar_io & ~3u);
                *ctl = (uint16_t)((bar_ctl & ~3u) + 2);
            }
        }
        return;
    }
}

/* Is this drive the disk we booted from? (Same first sector as boot0.) */
static int is_boot_disk(const struct disk *d)
{
    static uint8_t mine[BLK_SECTOR], boot[BLK_SECTOR];
    device_t *b = device_find("boot0");
    struct blk_io io = { 0, 1, boot };
    if (!b || b->state != DEVICE_BOUND || dev_ioctl(b, BLK_READ, &io) < 0)
        return 0;
    return read_sectors(d, 0, 1, mine) == 0 && !memcmp(mine, boot, BLK_SECTOR);
}

static int ata_init(device_t *dev)
{
    (void)dev;
    static uint16_t id[256];
    int skipped = 0;
    for (int pos = 0; pos < 4; pos++) {
        struct disk d = { .slave = pos & 1 };
        channel_ports(pos >> 1, &d.io, &d.ctl);
        if (identify(&d, id))
            continue;
        d.lba48 = (id[83] & (1 << 10)) != 0;
        d.sectors = d.lba48 && !id[102] && !id[103] ? id[100] | (uint32_t)id[101] << 16
                                                     : id[60] | (uint32_t)id[61] << 16;
        if (!d.sectors)
            continue;
        if (is_boot_disk(&d)) {
            skipped++;
            continue;
        }
        disk = d;
        info = (struct blk_info){ .sectors = d.sectors, .read_only = 1 };
        int n = 0;
        for (int w = 27; w <= 46 && n < (int)sizeof(info.model) - 2; w++) {
            info.model[n++] = (char)(id[w] >> 8);
            info.model[n++] = (char)id[w];
        }
        while (n > 0 && info.model[n - 1] == ' ')
            n--;
        info.model[n] = '\0';
        kprintf("ata0: %s, %u MiB, %s %s (read-only)\n", info.model, d.sectors / 2048,
                pos >> 1 ? "secondary" : "primary", d.slave ? "slave" : "master");
        return 0;
    }
    kprintf("ata0: no internal disk%s\n", skipped ? " (only the one we booted from)" : "");
    return -ENODEV;
}

static int ata_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev;
    struct blk_io *io = arg;
    switch (cmd) {
    case BLK_GET_INFO:
        if (!arg)
            return -EINVAL;
        *(struct blk_info *)arg = info;
        return 0;
    case BLK_READ:
        if (!io || !io->buf || io->lba >= info.sectors || io->count > info.sectors - io->lba)
            return -EINVAL;
        for (uint32_t done = 0; done < io->count;) {
            uint32_t n = io->count - done < 256 ? io->count - done : 256;
            int err = read_sectors(&disk, io->lba + done, n, (uint8_t *)io->buf + done * BLK_SECTOR);
            if (err)
                return err;
            done += n;
        }
        return 0;
    case BLK_WRITE:
        return -EROFS;                      /* never: see the top of this file */
    default:
        return -ENOSYS;
    }
}

const driver_t ata_driver = {
    .name = "ata-pio",
    .pci_ids = NULL,
    .init = ata_init,
    .read = driver_nosys_read,
    .write = driver_nosys_write,
    .ioctl = ata_ioctl,
    .shutdown = driver_noop_shutdown,
};

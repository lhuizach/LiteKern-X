/* LiteKern X — the boot disk through the BIOS (Phase 2 §5a), ported from v1's
 * drivers/bios_disk.c. Legacy device "boot0"; legacy_arg is the BIOS drive
 * number stage 2 booted from (boot_info.boot_drive). Interface: kernel/block.h.
 *
 * Each request drops to real mode for one INT 13h extended read or write
 * (boot/bios_thunk.asm). That reaches whatever the BIOS booted from, which on
 * the EeePC is the USB stick: there is no USB stack (docs/NON-GOALS.md), and
 * the BIOS's own USB support is how stage 2 read the kernel in the first
 * place. It's slow-ish and stops interrupts during each call, which is fine
 * for a file manager.
 *
 * Transfers go through a buffer next to the thunk, in the low memory the BIOS
 * can address, at most CHUNK sectors per call. */
#include "drivers/builtin.h"
#include "kernel/block.h"
#include "kernel/errno.h"
#include "kernel/printk.h"
#include "kernel/string.h"

#define THUNK_BASE  0x20000u    /* boot/bios_thunk.asm's BASE */
#define PARAMS      0x400u
#define PACKET      0x420u
#define BUFFER      0x1000u     /* offset: segment 0x2000, offset 0x1000 */
#define CHUNK       64u         /* sectors per call (32 KiB, inside one 64 KiB segment) */
#define SMALL_CHUNK 8u          /* when a whole chunk keeps failing */
#define TRIES       3           /* per call, with a drive reset between */

#define INT13_RESET      0x00
#define INT13_CHECK_EXT  0x41
#define INT13_READ       0x42
#define INT13_WRITE      0x43
#define INT13_PARAMS     0x48
#define FLAG_CARRY       0x0001

extern const uint8_t bios_thunk_blob[];
extern const uint32_t bios_thunk_blob_size;

struct regs {
    uint16_t ax, bx, cx, dx, si;
};

struct __attribute__((packed)) params {
    struct regs in;
    uint16_t out_ax, out_bx, out_cx, out_dx, out_flags;
};

struct __attribute__((packed)) dap {
    uint8_t size, zero;
    uint16_t count;
    uint16_t offset, segment;
    uint64_t lba;
};

struct __attribute__((packed)) drive_params {
    uint16_t size, flags;
    uint32_t cylinders, heads, sectors_per_track;
    uint64_t sectors;
    uint16_t bytes_per_sector;
};

static uint8_t drive;
static struct blk_info info;
static uint8_t *const low = (uint8_t *)THUNK_BASE;

/* One INT 13h call. Returns the BIOS's AH on failure (carry set), else 0. */
static int bios_call(struct regs in, struct params *out)
{
    struct params *p = (struct params *)(low + PARAMS);
    p->in = in;
    void (*thunk)(void) = (void (*)(void))THUNK_BASE;
    thunk();
    if (out)
        *out = *p;
    return (p->out_flags & FLAG_CARRY) ? (p->out_ax >> 8) | 0x100 : 0;
}

/* One INT 13h transfer, as it is. The BIOS's AH (| 0x100) on failure, else 0. */
static int transfer_once(uint8_t fn, uint32_t lba, uint32_t count)
{
#ifdef LKX_FAKE_TIMEOUTS
    static uint32_t calls;
    if (count == CHUNK && calls++ % 2 == 0)
        return 0x180;                       /* pretend the BIOS timed out */
#endif
    struct dap *d = (struct dap *)(low + PACKET);
    *d = (struct dap){ .size = sizeof(struct dap), .count = (uint16_t)count, .offset = BUFFER,
                       .segment = THUNK_BASE >> 4, .lba = lba };
    return bios_call((struct regs){ .ax = (uint16_t)(fn << 8), .dx = drive, .si = PACKET }, 0);
}

/* The same, with what real bootloaders do: on an error (the EeePC's BIOS
 * answers AH=0x80, "timeout", when the stick has been idle a while), reset
 * the drive and try again; if a whole chunk keeps failing, go a few sectors
 * at a time. Logged when it took more than one try. */
static int transfer(uint8_t fn, uint32_t lba, uint32_t count)
{
    int err = 0;
    for (int attempt = 0; attempt < TRIES; attempt++) {
        if (attempt)
            bios_call((struct regs){ .ax = INT13_RESET << 8, .dx = drive }, 0);
        err = transfer_once(fn, lba, count);
        if (!err) {
            if (attempt)
                kprintf("boot0: BIOS %s of %u sectors at %u worked on try %d\n",
                        fn == INT13_READ ? "read" : "write", count, lba, attempt + 1);
            return 0;
        }
    }
    if (count > SMALL_CHUNK && fn == INT13_READ) {
        /* Smaller pieces; the buffer stays where the caller expects it. */
        static uint8_t keep[CHUNK * BLK_SECTOR];
        for (uint32_t done = 0; done < count; done += SMALL_CHUNK) {
            uint32_t n = count - done < SMALL_CHUNK ? count - done : SMALL_CHUNK;
            if (transfer(fn, lba + done, n))
                goto failed;
            memcpy(keep + done * BLK_SECTOR, low + BUFFER, n * BLK_SECTOR);
        }
        memcpy(low + BUFFER, keep, count * BLK_SECTOR);
        kprintf("boot0: BIOS read of %u sectors at %u worked %u at a time\n", count, lba, SMALL_CHUNK);
        return 0;
    }
failed:
    kprintf("boot0: BIOS %s of %u sectors at %u failed (AH=0x%02x)\n",
            fn == INT13_READ ? "read" : "write", count, lba, err & 0xff);
    return -EIO;
}

static int bios_disk_init(device_t *dev)
{
    drive = (uint8_t)dev->legacy_arg;
    if (drive < 0x80)
        return -ENODEV;             /* a floppy: not something we write to */
    memcpy(low, bios_thunk_blob, bios_thunk_blob_size);

    struct params out;
    if (bios_call((struct regs){ .ax = INT13_CHECK_EXT << 8, .bx = 0x55aa, .dx = drive }, &out) ||
        out.out_bx != 0xaa55 || !(out.out_cx & 1)) {
        kprintf("boot0: the BIOS has no LBA disk calls for drive 0x%02x\n", drive);
        return -ENODEV;
    }

    struct drive_params *dp = (struct drive_params *)(low + PACKET);
    memset(dp, 0, 0x40);
    dp->size = 0x1e;
    info = (struct blk_info){ .removable = 1 };
    if (!bios_call((struct regs){ .ax = INT13_PARAMS << 8, .dx = drive, .si = PACKET }, 0) &&
        dp->bytes_per_sector == BLK_SECTOR && dp->sectors && dp->sectors < 0xffffffffull)
        info.sectors = (uint32_t)dp->sectors;
    const char *m = "BIOS drive 0x";
    int n = 0;
    for (; m[n]; n++)
        info.model[n] = m[n];
    info.model[n++] = "0123456789abcdef"[drive >> 4];
    info.model[n++] = "0123456789abcdef"[drive & 15];
    info.model[n] = '\0';
    return 0;
}

static int rw(struct blk_io *io, int write)
{
    if (!io || !io->buf)
        return -EINVAL;
    if (info.sectors && (io->lba >= info.sectors || io->count > info.sectors - io->lba))
        return -EINVAL;
    uint8_t *buf = io->buf;
    for (uint32_t done = 0; done < io->count;) {
        uint32_t n = io->count - done < CHUNK ? io->count - done : CHUNK;
        if (write)
            memcpy(low + BUFFER, buf + done * BLK_SECTOR, n * BLK_SECTOR);
        int err = transfer(write ? INT13_WRITE : INT13_READ, io->lba + done, n);
        if (err)
            return err;
        if (!write)
            memcpy(buf + done * BLK_SECTOR, low + BUFFER, n * BLK_SECTOR);
        done += n;
    }
    return 0;
}

static int bios_disk_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev;
    switch (cmd) {
    case BLK_GET_INFO:
        if (!arg)
            return -EINVAL;
        *(struct blk_info *)arg = info;
        return 0;
    case BLK_READ:
        return rw(arg, 0);
    case BLK_WRITE:
        return rw(arg, 1);
    default:
        return -ENOSYS;
    }
}

const driver_t bios_disk_driver = {
    .name = "bios-disk",
    .pci_ids = NULL,
    .init = bios_disk_init,
    .read = driver_nosys_read,
    .write = driver_nosys_write,
    .ioctl = bios_disk_ioctl,
    .shutdown = driver_noop_shutdown,
};

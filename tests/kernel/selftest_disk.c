/* LiteKern X — BIOS disk self-test (Phase 2 §5a). Test builds only
 * (-DLKX_SELFTEST_DISK); see tests/kernel/test-kernel.sh, which boots a
 * scratch copy of the image padded with 1 MiB of zeros, so the last sector
 * is free to write. The script then checks that sector in the image file
 * itself: the write must really reach the disk. */
#include "kernel/block.h"
#include "kernel/driver.h"
#include "kernel/errno.h"
#include "kernel/printk.h"
#include "kernel/timing.h"

static int passed, failed;
static uint8_t buf[64 * BLK_SECTOR];

static void check(int ok, const char *what)
{
    kprintf("selftest: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

static int io(device_t *d, unsigned cmd, uint32_t lba, uint32_t count, void *b)
{
    struct blk_io req = { lba, count, b };
    return dev_ioctl(d, cmd, &req);
}

void selftest_disk_run(void)
{
    device_t *d = device_find("boot0");
    check(d && d->state == DEVICE_BOUND, "boot0 is bound (the BIOS has LBA disk calls)");
    if (!d || d->state != DEVICE_BOUND)
        panic("disk self-test: no boot0");

    struct blk_info info;
    check(dev_ioctl(d, BLK_GET_INFO, &info) == 0 && info.sectors > 2048,
          "it reports its size");
    kprintf("selftest: boot0 is %u sectors (%u KiB), %s\n", info.sectors, info.sectors / 2,
            info.model);

    check(io(d, BLK_READ, 0, 1, buf) == 0 && buf[510] == 0x55 && buf[511] == 0xaa &&
              buf[440] == 'L' && buf[441] == 'K' && buf[442] == 'X',
          "sector 0 is our MBR (boot signature, \"LKX\" disk signature)");
    check(io(d, BLK_READ, info.sectors, 1, buf) == -EINVAL &&
              io(d, BLK_READ, info.sectors - 1, 2, buf) == -EINVAL,
          "reads past the end are refused");

    uint32_t last = info.sectors - 1;
    for (int i = 0; i < BLK_SECTOR; i++)
        buf[i] = (uint8_t)(i * 7 + 3);
    buf[0] = 'L', buf[1] = 'K', buf[2] = 'X', buf[3] = 'W';
    check(io(d, BLK_WRITE, last, 1, buf) == 0, "a write to the last sector succeeds");
    for (int i = 0; i < BLK_SECTOR; i++)
        buf[i] = 0;
    int same = io(d, BLK_READ, last, 1, buf) == 0 && buf[0] == 'L' && buf[3] == 'W';
    for (int i = 4; i < BLK_SECTOR && same; i++)
        same = buf[i] == (uint8_t)(i * 7 + 3);
    check(same, "reading it back gives what was written");

    /* 1 MiB in 64-sector requests, as a file manager would read a big file. */
    uint32_t t0 = uptime_ms(), ok = 1;
    for (uint32_t lba = 0; lba < 2048 && ok; lba += 64)
        ok = io(d, BLK_READ, lba, 64, buf) == 0;
    check(ok, "a 1 MiB read in 64-sector requests");
    kprintf("disk: read 1 MiB in %u ms\n", uptime_ms() - t0);

    kprintf("selftest: disk %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("disk self-test: %d checks failed", failed);
}

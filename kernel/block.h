/* LiteKern X — block devices (Phase 2 §5a): disks, read and written in
 * 512-byte sectors by LBA, through the driver interface's ioctl.
 *
 *   BLK_GET_INFO  struct blk_info *
 *   BLK_READ      struct blk_io *: read `count` sectors at `lba` into `buf`
 *   BLK_WRITE     struct blk_io *: write them; -EROFS on a read-only device
 *
 * Reads and writes return 0 or a negative error (-EINVAL past the end,
 * -EIO when the device fails). */
#ifndef LKX_BLOCK_H
#define LKX_BLOCK_H

#include <stdint.h>

#define BLK_SECTOR      512

#define BLK_GET_INFO    0x100
#define BLK_READ        0x101
#define BLK_WRITE       0x102

struct blk_info {
    uint32_t sectors;       /* 0 if the device can't say */
    uint8_t read_only;
    uint8_t removable;
    char model[24];         /* e.g. "BIOS drive 0x80" */
};

struct blk_io {
    uint32_t lba;
    uint32_t count;
    void *buf;
};

#endif

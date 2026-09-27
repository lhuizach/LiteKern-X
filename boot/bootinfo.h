/* LiteKern X — boot protocol structures (C side).
 * Must match boot/bootinfo.inc exactly; the static asserts below catch drift.
 * Documented in docs/BOOT-PROTOCOL.md. */
#ifndef LKX_BOOTINFO_H
#define LKX_BOOTINFO_H

#include <stdint.h>
#include <stddef.h>

#define BOOT_INFO_MAGIC   0x42584b4cu   /* 'LKXB' as nasm stores it */
#define BOOT_INFO_VERSION 1u
#define KERNEL_MAGIC      0x4b584b4cu   /* 'LKXK' */
#define KERNEL_VERSION    1u

#define BOOT_MMAP_MAX 32u       /* E820 entries stage 2 keeps */

#define BI_FLAG_FB             (1u << 0)
#define BI_FLAG_MMAP_TRUNCATED (1u << 1)

enum {
    TSC_STAGE1,         /* T0: stage 1 entry */
    TSC_STAGE2,         /* stage 2 entry */
    TSC_KERNEL_LOADED,  /* kernel read from disk  -> end of "bootloader" phase */
    TSC_VBE_DONE,       /* VBE mode set           -> end of "vbe" phase */
    TSC_KERNEL_ENTRY,   /* about to jump to the kernel */
    TSC_COUNT
};

/* One BIOS E820 memory map entry. */
struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;      /* 1 = usable RAM */
    uint32_t acpi;
} __attribute__((packed));

/* Handed to the kernel: EAX = BOOT_INFO_MAGIC, EBX = physical address of this. */
struct boot_info {
    uint32_t magic;
    uint32_t version;
    uint32_t boot_drive;
    uint32_t flags;
    uint64_t tsc[TSC_COUNT];
    uint32_t mmap_addr;         /* physical address of struct e820_entry[mmap_count] */
    uint32_t mmap_count;
    uint32_t fb_addr;           /* linear framebuffer, 32 bpp */
    uint32_t fb_pitch;          /* bytes per scanline */
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_bpp;
    uint32_t kernel_start;
    uint32_t kernel_end;
} __attribute__((packed));

/* First bytes of the kernel image on disk. */
struct kernel_header {
    uint32_t magic;
    uint32_t version;
    uint32_t load_addr;
    uint32_t entry;
    uint32_t file_size;
    uint32_t mem_end;
} __attribute__((packed));

_Static_assert(sizeof(struct e820_entry) == 24, "e820_entry layout");
_Static_assert(offsetof(struct boot_info, tsc) == 16, "boot_info layout");
_Static_assert(offsetof(struct boot_info, mmap_addr) == 56, "boot_info layout");
_Static_assert(offsetof(struct boot_info, fb_addr) == 64, "boot_info layout");
_Static_assert(offsetof(struct boot_info, kernel_end) == 88, "boot_info layout");
_Static_assert(sizeof(struct boot_info) == 92, "boot_info layout");
_Static_assert(sizeof(struct kernel_header) == 24, "kernel_header layout");

#endif

/* LiteKern X — boot protocol structures (C side).
 * Must match boot/bootinfo.inc exactly; the static asserts below catch drift.
 * Documented in docs/BOOT-PROTOCOL.md. */
#ifndef LKX_BOOTINFO_H
#define LKX_BOOTINFO_H

#include <stdint.h>
#include <stddef.h>

#define BOOT_INFO_MAGIC   0x42584b4cu   /* 'LKXB' as nasm stores it */
#define BOOT_INFO_VERSION 3u    /* 2: adds font_addr; 3: adds the VBE report */
#define BOOT_VBE_MODES_MAX 64u
#define VBE_OEM_MAX 48u
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
    uint32_t font_addr;         /* video BIOS 8x16 font: 256 glyphs x 16 bytes, 0 if none */
    uint32_t vbe_version;       /* BCD, e.g. 0x0300 = VBE 3.0 */
    uint32_t vbe_mem_kb;        /* video memory the BIOS reports */
    uint32_t vbe_modes_addr;    /* struct vbe_mode_entry[vbe_modes_count], in page 0 */
    uint32_t vbe_modes_count;
    char vbe_oem[VBE_OEM_MAX];  /* the video BIOS's name, NUL-terminated, truncated */
} __attribute__((packed));

/* One VBE mode as the video BIOS described it. Stage 2 records every mode it
 * got an answer for, until its walk stops at a 1024x600 match or the list ends. */
struct vbe_mode_entry {
    uint16_t mode;
    uint16_t width, height;
    uint8_t bpp;
    uint8_t model;              /* 4 packed pixel, 6 direct colour */
    uint16_t attributes;        /* bit 0 supported, 4 graphics, 7 linear framebuffer */
    uint16_t reserved;
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
_Static_assert(offsetof(struct boot_info, font_addr) == 92, "boot_info layout");
_Static_assert(offsetof(struct boot_info, vbe_version) == 96, "boot_info layout");
_Static_assert(offsetof(struct boot_info, vbe_oem) == 112, "boot_info layout");
_Static_assert(sizeof(struct boot_info) == 160, "boot_info layout");
_Static_assert(sizeof(struct vbe_mode_entry) == 12, "vbe_mode_entry layout");
_Static_assert(sizeof(struct kernel_header) == 24, "kernel_header layout");

#endif

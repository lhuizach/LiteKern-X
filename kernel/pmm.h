/* LiteKern X — physical memory manager: a bitmap of 4 KiB frames (Phase 1 §5).
 *
 * Only frames the kernel can reach through its identity map are managed:
 * usable E820 RAM from 1 MiB up to PMM_LIMIT, minus the kernel image. A frame's
 * physical address is also a valid kernel pointer. */
#ifndef LKX_PMM_H
#define LKX_PMM_H

#include <stdint.h>
#include "boot/bootinfo.h"

#define PAGE_SIZE   4096u
#define PMM_LIMIT   0x80000000u     /* 2 GiB: where user space starts (kernel/user.h) */

void pmm_init(const struct boot_info *bi);

/* A zero-filled frame, or 0 if memory is exhausted. */
uint32_t pmm_alloc(void);

/* `count` physically contiguous zero-filled frames, or 0. For buffers the
 * kernel addresses as one block through its identity map (the screen's back
 * buffer). */
uint32_t pmm_alloc_contiguous(uint32_t count);

/* Panics on a misaligned, unmanaged or already-free frame. */
void pmm_free(uint32_t phys);

uint32_t pmm_free_frames(void);
uint32_t pmm_total_frames(void);

/* End of RAM rounded up to 4 MiB: how far the kernel's identity map reaches. */
uint32_t pmm_direct_map_end(void);

#endif

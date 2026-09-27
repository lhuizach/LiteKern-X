/* LiteKern X — paging (Phase 1 §5). 32-bit, non-PAE, one address space.
 *
 * Virtual layout:
 *   0x00000000            page 0: NOT mapped (null pointers fault)
 *   0x00001000–RAM end    identity map of physical RAM, supervisor-only.
 *                         Kernel code + rodata (up to __ro_end) read-only;
 *                         4 KiB pages below 4 MiB, 4 MiB pages above.
 *   0x80000000–0xBFFFFFFF user space (USER_BASE..USER_TOP): the only range
 *                         ring 3 can touch; page tables made on demand.
 *   framebuffer           identity-mapped, supervisor-only, wherever the BIOS
 *                         put it — must not overlap user space.
 * CR0.WP is set, so read-only pages bind the kernel too. */
#ifndef LKX_VMM_H
#define LKX_VMM_H

#include <stdint.h>
#include "boot/bootinfo.h"

#define PTE_PRESENT 0x001u
#define PTE_WRITE   0x002u
#define PTE_USER    0x004u
#define PDE_4MIB    0x080u

void vmm_init(const struct boot_info *bi);

/* Map one user page. Returns 0, -EINVAL (not a page-aligned user address,
 * or already mapped) or -ENOMEM (no frame for a page table). */
int vmm_map_user(uint32_t virt, uint32_t phys, int writable);

/* The page-table entry for `virt` (0 if unmapped). 4 MiB pages report their
 * directory entry. */
uint32_t vmm_lookup(uint32_t virt);

/* Unmap all of user space, freeing every mapped frame and page table. */
void vmm_user_teardown(void);

void vmm_report(void);

#endif

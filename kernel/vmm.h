/* LiteKern X — paging (Phase 1 §5). 32-bit, non-PAE, one address space.
 *
 * Each app has its own address space (a page directory): the kernel's half
 * is the same in all of them, the user half is the app's own. The kernel's
 * own directory (address space 0) has no user pages.
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
#define PTE_SHARED  0x200u      /* (available bit) the frame isn't this space's to free */

void vmm_init(const struct boot_info *bi);

/* Map one user page in the current address space. flags: PTE_WRITE,
 * PTE_SHARED (not freed with the space: a window's pixels). Returns 0,
 * -EINVAL (not a page-aligned user address, or already mapped) or -ENOMEM
 * (no frame for a page table). */
int vmm_map_user(uint32_t virt, uint32_t phys, int flags);

/* The page-table entry for `virt` (0 if unmapped). 4 MiB pages report their
 * directory entry. */
uint32_t vmm_lookup(uint32_t virt);

/* Unmap all of the current space's user half, freeing every frame it owns
 * and its page tables. */
void vmm_user_teardown(void);

/* Address spaces (one per app). create: a new one with an empty user half,
 * or 0 if out of memory. switch: load it into CR3 (0: the kernel's own).
 * destroy: free it and everything it owns; never the one in use. */
uint32_t vmm_space_create(void);
void vmm_space_switch(uint32_t pd);
uint32_t vmm_space_current(void);
void vmm_space_destroy(uint32_t pd);

/* Identity-map [phys, phys + len) read-only for the kernel if it isn't
 * already (firmware tables above RAM, e.g. ACPI's). At boot only: address
 * spaces made earlier don't get it. 0 or -EINVAL (it would cover user space). */
int vmm_map_firmware(uint32_t phys, uint32_t len);

void vmm_report(void);

#endif

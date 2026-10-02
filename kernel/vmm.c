#include "kernel/vmm.h"
#include "kernel/errno.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/user.h"

#define MIB4        0x400000u
#define PD_INDEX(a) ((a) >> 22)
#define PT_INDEX(a) (((a) >> 12) & 0x3ff)

extern char __kernel_start[], __ro_end[];

static uint32_t kernel_pd[1024] __attribute__((aligned(4096)));
static uint32_t low_pt[1024] __attribute__((aligned(4096)));    /* 0 – 4 MiB */
static uint32_t fb_start, fb_end;
static uint32_t *cur_pd = kernel_pd;    /* the address space in CR3 */

static void invlpg(uint32_t virt)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

static void reload_cr3(void)
{
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" : : : "eax", "memory");
}

void vmm_init(const struct boot_info *bi)
{
    uint32_t ro_start = (uint32_t)__kernel_start, ro_end = (uint32_t)__ro_end;

    if ((uint32_t)__ro_end > MIB4)
        panic("vmm: kernel code extends past 4 MiB (0x%08x)", ro_end);

    /* 0 – 4 MiB in 4 KiB pages: page 0 absent, kernel code read-only. */
    for (uint32_t i = 1; i < 1024; i++) {
        uint32_t addr = i * PAGE_SIZE;
        low_pt[i] = addr | PTE_PRESENT;
        if (addr < ro_start || addr >= ro_end)
            low_pt[i] |= PTE_WRITE;
    }
    kernel_pd[0] = (uint32_t)low_pt | PTE_PRESENT | PTE_WRITE;

    /* The rest of RAM in 4 MiB pages. */
    for (uint32_t addr = MIB4; addr < pmm_direct_map_end(); addr += MIB4)
        kernel_pd[PD_INDEX(addr)] = addr | PTE_PRESENT | PTE_WRITE | PDE_4MIB;

    /* Framebuffer (above RAM, in the PCI hole). */
    if (bi->flags & BI_FLAG_FB) {
        fb_start = bi->fb_addr & ~(MIB4 - 1);
        fb_end = bi->fb_addr + bi->fb_pitch * bi->fb_height;
        if (fb_end > USER_BASE && fb_start < USER_TOP)
            panic("vmm: framebuffer 0x%08x-0x%08x overlaps user space", bi->fb_addr, fb_end);
        for (uint32_t addr = fb_start; addr < fb_end && addr >= fb_start; addr += MIB4)
            if (!kernel_pd[PD_INDEX(addr)])
                kernel_pd[PD_INDEX(addr)] = addr | PTE_PRESENT | PTE_WRITE | PDE_4MIB;
    }

    __asm__ volatile(
        "mov %0, %%cr3\n\t"
        "mov %%cr4, %%eax\n\t"
        "or $0x10, %%eax\n\t"           /* CR4.PSE: 4 MiB pages */
        "mov %%eax, %%cr4\n\t"
        "mov %%cr0, %%eax\n\t"
        "or $0x80010000, %%eax\n\t"     /* CR0.PG | CR0.WP */
        "mov %%eax, %%cr0"
        :
        : "r"(kernel_pd)
        : "eax", "memory");
}

int vmm_map_firmware(uint32_t phys, uint32_t len)
{
    if (!len || phys + len < phys)
        return -EINVAL;
    for (uint32_t a = phys & ~(MIB4 - 1); a < phys + len && a >= (phys & ~(MIB4 - 1)); a += MIB4) {
        if (a >= USER_BASE && a < USER_TOP)
            return -EINVAL;                     /* never over user space */
        if (!kernel_pd[PD_INDEX(a)])
            kernel_pd[PD_INDEX(a)] = a | PTE_PRESENT | PDE_4MIB;    /* read-only */
    }
    reload_cr3();
    return 0;
}

static int is_user_page(uint32_t virt)
{
    return virt >= USER_BASE && virt < USER_TOP && !(virt % PAGE_SIZE);
}

int vmm_map_user(uint32_t virt, uint32_t phys, int flags)
{
    if (!is_user_page(virt) || phys % PAGE_SIZE)
        return -EINVAL;
    uint32_t *pde = &cur_pd[PD_INDEX(virt)];
    if (!(*pde & PTE_PRESENT)) {
        uint32_t pt = pmm_alloc();
        if (!pt)
            return -ENOMEM;
        *pde = pt | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }
    uint32_t *pt = (uint32_t *)(*pde & ~0xfffu);
    if (pt[PT_INDEX(virt)] & PTE_PRESENT)
        return -EINVAL;
    pt[PT_INDEX(virt)] = phys | PTE_PRESENT | PTE_USER | (flags & (PTE_WRITE | PTE_SHARED));
    invlpg(virt);
    return 0;
}

uint32_t vmm_lookup(uint32_t virt)
{
    uint32_t pde = cur_pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT) || (pde & PDE_4MIB))
        return pde;
    uint32_t pte = ((uint32_t *)(pde & ~0xfffu))[PT_INDEX(virt)];
    return (pte & PTE_PRESENT) ? pte : 0;
}

/* Free the user half of `pd`: its page tables and every frame it owns
 * (PTE_SHARED frames belong to someone else). */
static void free_user_half(uint32_t *pd)
{
    for (uint32_t i = PD_INDEX(USER_BASE); i < PD_INDEX(USER_TOP); i++) {
        if (!(pd[i] & PTE_PRESENT))
            continue;
        uint32_t *pt = (uint32_t *)(pd[i] & ~0xfffu);
        for (uint32_t j = 0; j < 1024; j++)
            if ((pt[j] & PTE_PRESENT) && !(pt[j] & PTE_SHARED))
                pmm_free(pt[j] & ~0xfffu);
        pmm_free((uint32_t)pt);
        pd[i] = 0;
    }
}

void vmm_user_teardown(void)
{
    free_user_half(cur_pd);
    reload_cr3();
}

uint32_t vmm_space_create(void)
{
    uint32_t *pd = (uint32_t *)pmm_alloc();     /* zeroed: no user pages yet */
    if (!pd)
        return 0;
    for (uint32_t i = 0; i < 1024; i++)
        if (i < PD_INDEX(USER_BASE) || i >= PD_INDEX(USER_TOP))
            pd[i] = kernel_pd[i];               /* the kernel's half, shared */
    return (uint32_t)pd;
}

void vmm_space_switch(uint32_t pd)
{
    uint32_t *next = pd ? (uint32_t *)pd : kernel_pd;
    if (next == cur_pd)
        return;
    cur_pd = next;
    __asm__ volatile("mov %0, %%cr3" : : "r"(cur_pd) : "memory");
}

uint32_t vmm_space_current(void)
{
    return cur_pd == kernel_pd ? 0 : (uint32_t)cur_pd;
}

void vmm_space_destroy(uint32_t pd)
{
    if (!pd || (uint32_t *)pd == cur_pd)
        panic("vmm: destroying the address space in use");
    free_user_half((uint32_t *)pd);
    pmm_free(pd);
}

void vmm_report(void)
{
    kprintf("mm: paging on; null page unmapped; kernel code read-only 0x%08x-0x%08x\n",
            (uint32_t)__kernel_start, (uint32_t)__ro_end - 1);
    kprintf("mm: RAM identity-mapped to 0x%08x; user space 0x%08x-0x%08x\n",
            pmm_direct_map_end(), USER_BASE, USER_TOP - 1);
    if (fb_end)
        kprintf("mm: framebuffer mapped 0x%08x-0x%08x\n", fb_start, fb_end - 1);
    kprintf("mm: %u MiB free of %u MiB managed\n", pmm_free_frames() / 256,
            pmm_total_frames() / 256);
}

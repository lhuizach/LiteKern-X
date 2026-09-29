#include "kernel/mtrr.h"
#include "kernel/cpu.h"

#define MSR_MTRRCAP         0x0fe
#define MSR_MTRR_DEF_TYPE   0x2ff
#define MSR_MTRR_PHYSBASE0  0x200
#define MTRR_DEF_ENABLE     (1u << 11)
#define MTRR_MASK_VALID     (1u << 11)
#define MTRR_TYPE_WC        1u

#define CPUID_EDX_MSR       (1u << 5)
#define CPUID_EDX_MTRR      (1u << 12)

static uint32_t phys_bits(void)
{
    uint32_t a, b, c, d;
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000008u)
        return 36;          /* SDM fallback for CPUs without the leaf */
    cpuid(0x80000008u, &a, &b, &c, &d);
    return a & 0xff;
}

/* Smallest power-of-two, naturally aligned range covering [phys, phys+size). */
static void round_range(uint32_t phys, uint32_t size, uint64_t *base, uint64_t *len)
{
    uint64_t l = 0x1000;
    while (l < size)
        l <<= 1;
    for (;;) {
        uint64_t b = phys & ~(l - 1);
        if (b + l >= (uint64_t)phys + size) {
            *base = b;
            *len = l;
            return;
        }
        l <<= 1;
    }
}

static int overlaps_ram(uint64_t base, uint64_t len, const struct boot_info *bi)
{
    const struct e820_entry *e = (const struct e820_entry *)bi->mmap_addr;
    for (uint32_t i = 0; i < bi->mmap_count; i++)
        if (e[i].type == 1 && e[i].base < base + len && base < e[i].base + e[i].length)
            return 1;
    return 0;
}

enum mtrr_result mtrr_set_write_combining(uint32_t phys, uint32_t size,
                                          const struct boot_info *bi,
                                          uint32_t *base_out, uint32_t *len_out)
{
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(d & CPUID_EDX_MSR) || !(d & CPUID_EDX_MTRR))
        return MTRR_NOT_SUPPORTED;
    uint32_t count = rdmsr(MSR_MTRRCAP) & 0xff;
    if (!count)
        return MTRR_NOT_SUPPORTED;

    uint64_t base, len;
    round_range(phys, size, &base, &len);
    if (base + len > 0x100000000ull || overlaps_ram(base, len, bi))
        return MTRR_UNSAFE_RANGE;

    uint32_t slot = count;
    for (uint32_t i = 0; i < count; i++)
        if (!(rdmsr(MSR_MTRR_PHYSBASE0 + i * 2 + 1) & MTRR_MASK_VALID)) {
            slot = i;
            break;
        }
    if (slot == count)
        return MTRR_NO_FREE_SLOT;

    uint64_t addr_mask = (1ull << phys_bits()) - 1;

    /* Intel SDM Vol. 3, 11.11.8: caches off (CD=1, NW=0) and flushed, TLB
     * flushed, MTRRs disabled; update; flush again; re-enable. */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0; wbinvd" : : "r"((cr0 | (1u << 30)) & ~(1u << 29)) : "memory");
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" : : : "eax", "memory");
    uint64_t def = rdmsr(MSR_MTRR_DEF_TYPE);
    wrmsr(MSR_MTRR_DEF_TYPE, def & ~(uint64_t)MTRR_DEF_ENABLE);

    wrmsr(MSR_MTRR_PHYSBASE0 + slot * 2, (base & addr_mask) | MTRR_TYPE_WC);
    wrmsr(MSR_MTRR_PHYSBASE0 + slot * 2 + 1, (addr_mask & ~(len - 1)) | MTRR_MASK_VALID);

    __asm__ volatile("wbinvd" ::: "memory");
    __asm__ volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" : : : "eax", "memory");
    wrmsr(MSR_MTRR_DEF_TYPE, def);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    *base_out = (uint32_t)base;
    *len_out = (uint32_t)len;
    return MTRR_OK;
}

const char *mtrr_result_name(enum mtrr_result r)
{
    switch (r) {
    case MTRR_OK:             return "on";
    case MTRR_NOT_SUPPORTED:  return "off (CPU has no MTRRs)";
    case MTRR_NO_FREE_SLOT:   return "off (no free MTRR)";
    case MTRR_UNSAFE_RANGE:   return "off (aligned range would cover RAM)";
    }
    return "off";
}

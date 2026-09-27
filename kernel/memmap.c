#include "kernel/memmap.h"
#include "kernel/printk.h"

static const char *type_name(uint32_t type)
{
    switch (type) {
    case 1: return "usable";
    case 2: return "reserved";
    case 3: return "ACPI reclaimable";
    case 4: return "ACPI NVS";
    case 5: return "bad memory";
    default: return "unknown";
    }
}

uint32_t memmap_report(const struct boot_info *bi)
{
    const struct e820_entry *e = (const struct e820_entry *)bi->mmap_addr;
    uint64_t usable = 0, usable_high = 0;

    for (uint32_t i = 0; i < bi->mmap_count; i++) {
        kprintf("mem 0x%016llx-0x%016llx %s", e[i].base, e[i].base + e[i].length - 1,
                type_name(e[i].type));
        if (e[i].type > 5)
            kprintf(" (type %u)", e[i].type);
        kprintf("\n");

        if (e[i].type != 1)
            continue;
        usable += e[i].length;
        uint64_t end = e[i].base + e[i].length;
        if (end > 0x100000)
            usable_high += end - (e[i].base > 0x100000 ? e[i].base : 0x100000);
    }
    if (bi->flags & BI_FLAG_MMAP_TRUNCATED)
        kprintf("mem: warning: BIOS reported more than %u regions; the rest were dropped\n",
                bi->mmap_count);
    if (!usable_high)
        panic("memory map has no usable RAM above 1 MiB");
    kprintf("mem: %u MiB usable in %u regions\n", (uint32_t)(usable >> 20), bi->mmap_count);

    if (bi->flags & BI_FLAG_FB)
        kprintf("fb %ux%ux%u pitch=%u at 0x%08x\n", bi->fb_width, bi->fb_height, bi->fb_bpp,
                bi->fb_pitch, bi->fb_addr);
    else
        kprintf("fb: none\n");

    return (uint32_t)(usable >> 10);
}

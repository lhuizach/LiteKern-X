#include <stdint.h>
#include "kernel/gdt.h"

struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t base_mid;
    uint8_t access;
    uint8_t flags_limit_hi;     /* flags in the high nibble */
    uint8_t base_hi;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Flat 4 GiB segment: base 0, limit 0xfffff in 4 KiB pages, 32-bit. */
#define FLAT(access) { 0xffff, 0, 0, (access), 0xcf, 0 }

static struct gdt_entry gdt[] = {
    { 0, 0, 0, 0, 0, 0 },       /* null */
    FLAT(0x9a),                 /* GDT_KCODE: present, ring 0, code, readable */
    FLAT(0x92),                 /* GDT_KDATA: present, ring 0, data, writable */
};

void gdt_init(void)
{
    struct gdt_ptr ptr = { sizeof(gdt) - 1, (uint32_t)gdt };

    __asm__ volatile(
        "lgdt %0\n\t"
        "ljmp %1, $1f\n"
        "1:\n\t"
        "mov %2, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss"
        :
        : "m"(ptr), "i"(GDT_KCODE), "i"(GDT_KDATA)
        : "eax", "memory");
}

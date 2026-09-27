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

/* 32-bit task state segment. Only esp0/ss0 are used: the stack the CPU
 * switches to when ring 3 traps into ring 0. iomap_base past the end means
 * no I/O permission bitmap, so ring 3 can't touch any port (IOPL is 0). */
struct tss {
    uint32_t prev, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

/* Flat 4 GiB segment: base 0, limit 0xfffff in 4 KiB pages, 32-bit. */
#define FLAT(access) { 0xffff, 0, 0, (access), 0xcf, 0 }

static struct gdt_entry gdt[] = {
    { 0, 0, 0, 0, 0, 0 },       /* null */
    FLAT(0x9a),                 /* GDT_KCODE: present, ring 0, code, readable */
    FLAT(0x92),                 /* GDT_KDATA: present, ring 0, data, writable */
    FLAT(0xfa),                 /* GDT_UCODE: present, ring 3, code, readable */
    FLAT(0xf2),                 /* GDT_UDATA: present, ring 3, data, writable */
    { 0, 0, 0, 0, 0, 0 },       /* GDT_TSS: filled in by gdt_init() */
};

static struct tss tss;

extern char trap_stack_top[];   /* kernel/usermode.asm */

void gdt_init(void)
{
    uint32_t base = (uint32_t)&tss, limit = sizeof(tss) - 1;
    tss.ss0 = GDT_KDATA;
    tss.esp0 = (uint32_t)trap_stack_top;
    tss.iomap_base = sizeof(tss);
    gdt[GDT_TSS / 8] = (struct gdt_entry){
        .limit_lo = limit & 0xffff,
        .base_lo = base & 0xffff,
        .base_mid = (base >> 16) & 0xff,
        .access = 0x89,             /* present, ring 0, 32-bit TSS (available) */
        .flags_limit_hi = (limit >> 16) & 0x0f,
        .base_hi = base >> 24,
    };

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
        "mov %%ax, %%ss\n\t"
        "mov %3, %%ax\n\t"
        "ltr %%ax"
        :
        : "m"(ptr), "i"(GDT_KCODE), "i"(GDT_KDATA), "i"(GDT_TSS)
        : "eax", "memory");
}

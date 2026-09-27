#include "kernel/idt.h"
#include "kernel/gdt.h"
#include "kernel/io.h"
#include "kernel/printk.h"

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define GATE_INT32_RING0 0x8e   /* present, DPL 0, 32-bit interrupt gate */

extern const uint32_t isr_stubs[32];

static struct idt_entry idt[256];

static const char *const exception_names[32] = {
    "#DE divide error", "#DB debug", "NMI", "#BP breakpoint",
    "#OF overflow", "#BR bound range", "#UD invalid opcode", "#NM device not available",
    "#DF double fault", "coprocessor segment overrun", "#TS invalid TSS", "#NP segment not present",
    "#SS stack fault", "#GP general protection", "#PF page fault", "reserved",
    "#MF x87 FP error", "#AC alignment check", "#MC machine check", "#XM SIMD FP error",
    "#VE virtualization", "#CP control protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved",
    "reserved", "reserved", "#SX security", "reserved",
};

void idt_init(void)
{
    for (int v = 0; v < 32; v++) {
        uint32_t addr = isr_stubs[v];
        idt[v] = (struct idt_entry){
            .offset_lo = addr & 0xffff,
            .selector = GDT_KCODE,
            .type_attr = GATE_INT32_RING0,
            .offset_hi = addr >> 16,
        };
    }
    struct idt_ptr ptr = { sizeof(idt) - 1, (uint32_t)idt };
    __asm__ volatile("lidt %0" : : "m"(ptr));
}

/* Every exception is fatal for now: report everything, then halt. A real
 * page fault handler comes with paging (Phase 1 §5). */
void exception_handler(struct int_frame *f)
{
    uint32_t v = f->vector;
    kprintf("\nexception %u (%s) error=0x%08x\n", v, exception_names[v & 31], f->error);
    kprintf("  eip=0x%08x cs=0x%04x eflags=0x%08x", f->eip, f->cs, f->eflags);
    if (v == 14)
        kprintf(" cr2=0x%08x", read_cr2());
    kprintf("\n  eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x\n", f->eax, f->ebx, f->ecx, f->edx);
    kprintf("  esi=0x%08x edi=0x%08x ebp=0x%08x esp=0x%08x\n",
            f->esi, f->edi, f->ebp,
            f->esp_at_pushad + 20);     /* interrupted ESP: skip vector, error, eip, cs, eflags */
    panic("unhandled CPU exception %u (%s)", v, exception_names[v & 31]);
}

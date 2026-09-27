#include "kernel/idt.h"
#include "kernel/gdt.h"
#include "kernel/io.h"
#include "kernel/printk.h"
#include "kernel/syscall.h"
#include "kernel/user.h"

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
#define GATE_INT32_RING3 0xee   /* present, DPL 3: reachable with `int` from ring 3 */

/* Page fault error code bits. */
#define PF_PRESENT  0x1         /* 0: page not present, 1: protection violation */
#define PF_WRITE    0x2
#define PF_USER     0x4

extern const uint32_t isr_stubs[32];
extern const char isr_syscall[];

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

static void set_gate(unsigned v, uint32_t addr, uint8_t type)
{
    idt[v] = (struct idt_entry){
        .offset_lo = addr & 0xffff,
        .selector = GDT_KCODE,
        .type_attr = type,
        .offset_hi = addr >> 16,
    };
}

void idt_init(void)
{
    for (unsigned v = 0; v < 32; v++)
        set_gate(v, isr_stubs[v], GATE_INT32_RING0);
    set_gate(SYSCALL_VECTOR, (uint32_t)isr_syscall, GATE_INT32_RING3);
    struct idt_ptr ptr = { sizeof(idt) - 1, (uint32_t)idt };
    __asm__ volatile("lidt %0" : : "m"(ptr));
}

static void describe_page_fault(const struct int_frame *f)
{
    kprintf("  page fault: %s %s 0x%08x (%s)\n",
            f->error & PF_USER ? "user" : "kernel",
            f->error & PF_WRITE ? "write to" : "read from", read_cr2(),
            f->error & PF_PRESENT ? "protection violation" : "page not present");
}

/* A fault in ring 3 kills the program; the kernel carries on. */
static void user_fault(const struct int_frame *f)
{
    uint32_t v = f->vector;
    kprintf("user: killed by exception %u (%s) at eip=0x%08x\n", v, exception_names[v], f->eip);
    if (v == 14)
        describe_page_fault(f);
    user_kill(f);
}

/* A fault in ring 0 is a kernel bug: report everything and halt. */
static void kernel_fault(const struct int_frame *f)
{
    uint32_t v = f->vector;
    kprintf("\nexception %u (%s) error=0x%08x\n", v, exception_names[v], f->error);
    if (v == 14)
        describe_page_fault(f);
    kprintf("  eip=0x%08x cs=0x%04x eflags=0x%08x\n", f->eip, f->cs, f->eflags);
    kprintf("  eax=0x%08x ebx=0x%08x ecx=0x%08x edx=0x%08x\n", f->eax, f->ebx, f->ecx, f->edx);
    kprintf("  esi=0x%08x edi=0x%08x ebp=0x%08x esp=0x%08x\n",
            f->esi, f->edi, f->ebp,
            f->esp_at_pushad + 20);     /* interrupted ESP: skip vector, error, eip, cs, eflags */
    if (v == 14)
        panic("page fault in the kernel at 0x%08x", read_cr2());
    panic("unhandled CPU exception %u (%s)", v, exception_names[v]);
}

void interrupt_handler(struct int_frame *f)
{
    int from_user = (f->cs & 3) == 3;

    if (f->vector == SYSCALL_VECTOR) {
        if (!from_user)
            panic("int 0x80 issued from ring 0 (eip=0x%08x)", f->eip);
        syscall_dispatch(f);
        return;
    }
    if (from_user)
        user_fault(f);
    kernel_fault(f);
}

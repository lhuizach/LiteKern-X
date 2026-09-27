/* LiteKern X — kernel entry point (called from kernel/entry.asm). */
#include "boot/bootinfo.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/io.h"
#include "kernel/printk.h"
#include "kernel/serial.h"
#include "kernel/status.h"
#include "kernel/timing.h"

void kmain(uint32_t magic, const struct boot_info *bi) __attribute__((noreturn));

void kmain(uint32_t magic, const struct boot_info *bi)
{
    serial_init();
    kprintf("\nLiteKern X\n");

    if (magic != BOOT_INFO_MAGIC || bi->magic != BOOT_INFO_MAGIC)
        panic("bad handoff: eax=0x%08x boot_info=%p", magic, (const void *)bi);
    if (bi->version != BOOT_INFO_VERSION)
        panic("boot_info version %u, kernel expects %u", bi->version, BOOT_INFO_VERSION);
    status_init(bi);

    gdt_init();
    idt_init();
    timing_init(bi);

#ifdef LKX_SELFTEST_FAULT
    /* Test builds only (tests/kernel): prove exceptions are caught and reported. */
    __asm__ volatile("ud2");
#endif

    uint64_t ready = rdtsc();
    boot_phase("bootloader", bi->tsc[TSC_STAGE1], bi->tsc[TSC_KERNEL_LOADED]);
    boot_phase("vbe", bi->tsc[TSC_KERNEL_LOADED], bi->tsc[TSC_VBE_DONE]);
    boot_phase("kernel_early", bi->tsc[TSC_VBE_DONE], ready);

    status_show(STATUS_READY);
    kprintf("[boot] ready t=%u\n", tsc_to_ms(ready - bi->tsc[TSC_STAGE1]));

    /* Nothing else exists yet (Phase 1 §3 onwards). */
    halt_forever();
}

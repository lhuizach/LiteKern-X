/* LiteKern X — kernel entry point (called from kernel/entry.asm). */
#include "boot/bootinfo.h"
#include "drivers/builtin.h"
#include "kernel/console.h"
#include "kernel/driver.h"
#include "kernel/gdt.h"
#include "kernel/idt.h"
#include "kernel/io.h"
#include "kernel/memmap.h"
#include "kernel/pci.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/serial.h"
#include "kernel/status.h"
#include "kernel/string.h"
#include "kernel/timing.h"
#include "kernel/vmm.h"

void kmain(uint32_t magic, const struct boot_info *handoff) __attribute__((noreturn));

/* Test builds only: tests/kernel/selftest_*.c, linked in via EXTRA_KERNEL_SRCS. */
#ifdef LKX_SELFTEST_DRIVERS
void selftest_drivers_register(void);
void selftest_drivers_run(void);
#endif
#ifdef LKX_SELFTEST_USER
void selftest_user_run(void);
#endif

/* boot_info and the E820 map live in page 0, which paging leaves unmapped
 * (so null pointers fault): keep the kernel's own copy. */
static struct boot_info boot_info;
static struct e820_entry boot_mmap[BOOT_MMAP_MAX];

static const struct boot_info *keep_boot_info(uint32_t magic, const struct boot_info *handoff)
{
    if (magic != BOOT_INFO_MAGIC || handoff->magic != BOOT_INFO_MAGIC)
        panic("bad handoff: eax=0x%08x boot_info=%p", magic, (const void *)handoff);
    if (handoff->version != BOOT_INFO_VERSION)
        panic("boot_info version %u, kernel expects %u", handoff->version, BOOT_INFO_VERSION);
    if (handoff->mmap_count > BOOT_MMAP_MAX)
        panic("boot_info has %u memory map entries, max %u", handoff->mmap_count, BOOT_MMAP_MAX);

    boot_info = *handoff;
    memcpy(boot_mmap, (const void *)handoff->mmap_addr,
           handoff->mmap_count * sizeof(struct e820_entry));
    boot_info.mmap_addr = (uint32_t)boot_mmap;
    return &boot_info;
}

void kmain(uint32_t magic, const struct boot_info *handoff)
{
    serial_init();
    kprintf("\nLiteKern X\n");

    const struct boot_info *bi = keep_boot_info(magic, handoff);
    status_init(bi);
    gdt_init();
    idt_init();
    timing_init(bi);

#ifdef LKX_SELFTEST_FAULT
    /* Test builds only (tests/kernel): prove exceptions are caught and reported. */
    __asm__ volatile("ud2");
#endif

    uint64_t early_done = rdtsc();
    boot_phase("bootloader", bi->tsc[TSC_STAGE1], bi->tsc[TSC_KERNEL_LOADED]);
    boot_phase("vbe", bi->tsc[TSC_KERNEL_LOADED], bi->tsc[TSC_VBE_DONE]);
    boot_phase("kernel_early", bi->tsc[TSC_VBE_DONE], early_done);

    /* Memory protection: frame allocator, then paging (Phase 1 §5). */
    pmm_init(bi);
    vmm_init(bi);
    uint64_t paging_done = rdtsc();
    boot_phase("paging", early_done, paging_done);

    /* Hardware detection: discover, initialise nothing (Phase 1 §3). */
    pci_scan();
    uint64_t pci_done = rdtsc();
    boot_phase("pci", paging_done, pci_done);

    /* Drivers: register, bind legacy devices, match PCI devices (Phase 1 §4). */
    drivers_register();
#ifdef LKX_SELFTEST_DRIVERS
    selftest_drivers_register();
#endif
    drivers_add_legacy_devices(bi);
    driver_probe_pci();
    uint64_t drivers_done = rdtsc();
    boot_phase("drivers", pci_done, drivers_done);

    /* First frame: the on-screen log (replaying everything so far) if the
     * display driver and font are there, else the plain status colour. */
    if (console_init(device_find("fb0"), bi->font_addr) < 0)
        kprintf("console: unavailable (no display or no BIOS font); status colour only\n");
    status_show(STATUS_READY);
    uint64_t ready = rdtsc();
    boot_phase("first_frame", drivers_done, ready);

    /* Everything is reported after the fact, so no phase above includes the
     * time spent printing (slow over serial in the VMs). */
    boot_report(ready);
    memmap_report(bi);
    vmm_report();
    pci_report();
    device_report();

#ifdef LKX_SELFTEST_DRIVERS
    selftest_drivers_run();
#endif
#ifdef LKX_SELFTEST_USER
    selftest_user_run();
#endif
#ifdef LKX_SELFTEST_KERNEL_NULL
    kprintf("selftest: kernel null-pointer write\n");
    *(volatile uint32_t *)0 = 1;
#endif
#ifdef LKX_SELFTEST_KERNEL_WP
    kprintf("selftest: kernel write to its own code\n");
    *(volatile uint8_t *)(uintptr_t)kmain = 0xcc;
#endif

    /* Nothing else exists yet (Phase 1 §6 onwards). */
    halt_forever();
}

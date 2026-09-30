/* LiteKern X — kernel entry point (called from kernel/entry.asm). */
#include "boot/bootinfo.h"
#include "drivers/builtin.h"
#include "kernel/apps.h"
#include "kernel/desktop.h"
#include "kernel/console.h"
#include "kernel/cursor.h"
#include "kernel/driver.h"
#include "kernel/screen.h"
#include "kernel/fb.h"
#include "kernel/gdt.h"
#include "kernel/mtrr.h"
#include "kernel/idt.h"
#include "kernel/input.h"
#include "kernel/io.h"
#include "kernel/irq.h"
#include "kernel/memmap.h"
#include "kernel/pci.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/serial.h"
#include "kernel/status.h"
#include "kernel/storage.h"
#include "kernel/string.h"
#include "kernel/timing.h"
#include "kernel/vbe.h"
#include "kernel/vmm.h"
#include "kernel/wm.h"

void kmain(uint32_t magic, const struct boot_info *handoff) __attribute__((noreturn));

/* Test builds only: tests/kernel/selftest_*.c, linked in via EXTRA_KERNEL_SRCS. */
#ifdef LKX_SELFTEST_DRIVERS
void selftest_drivers_register(void);
void selftest_drivers_run(void);
#endif
#ifdef LKX_SELFTEST_USER
void selftest_user_run(void);
#endif
#ifdef LKX_SELFTEST_GFX
void selftest_gfx_run(void);
#endif
#ifdef LKX_SELFTEST_WM
void selftest_wm_run(void);
#endif
#ifdef LKX_SELFTEST_WIDGETS
void selftest_widgets_run(void);
#endif
#ifdef LKX_SELFTEST_DISK
void selftest_disk_run(void);
#endif

/* boot_info and the E820 map live in page 0, which paging leaves unmapped
 * (so null pointers fault): keep the kernel's own copy. */
static struct boot_info boot_info;
static struct e820_entry boot_mmap[BOOT_MMAP_MAX];
static struct vbe_mode_entry boot_vbe_modes[BOOT_VBE_MODES_MAX];

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
    if (handoff->vbe_modes_count > BOOT_VBE_MODES_MAX)
        boot_info.vbe_modes_count = BOOT_VBE_MODES_MAX;
    if (handoff->vbe_modes_addr)
        memcpy(boot_vbe_modes, (const void *)handoff->vbe_modes_addr,
               boot_info.vbe_modes_count * sizeof(struct vbe_mode_entry));
    boot_info.vbe_modes_addr = (uint32_t)boot_vbe_modes;
    boot_info.vbe_oem[VBE_OEM_MAX - 1] = '\0';
    return &boot_info;
}

/* After boot: interrupts on, sleep until an IRQ, and pass input to the
 * desktop or the open app. Input is logged too, so the keyboard and
 * touchpad can still be checked on real hardware (in the Log app). */
#define MOUSE_LOG_MS 100    /* movement is logged at most this often */

/* Clicks go on screen; plain movement goes to serial only (the tests read it
 * there), as the cursor already shows it and each logged line used to cost a
 * console scroll, which made the cursor stutter on the EeePC. */
static void log_pointer(int x, int y, uint8_t buttons, int on_screen)
{
    void (*log)(const char *, ...) = on_screen ? kprintf : kdebugf;
    log("mouse: (%d, %d) buttons %c%c%c\n", x, y, buttons & MOUSE_LEFT ? 'L' : '-',
        buttons & MOUSE_MIDDLE ? 'M' : '-', buttons & MOUSE_RIGHT ? 'R' : '-');
}

static void __attribute__((noreturn)) idle(const struct boot_info *bi)
{
    device_t *kbd = device_find("kbd0"), *mouse = device_find("mouse0");
    int w = (int)bi->fb_width, h = (int)bi->fb_height;
    int x = w / 2, y = h / 2, moved = 0;
    uint8_t buttons = 0;
    uint32_t last_log = 0;

    if (kbd && kbd->state == DEVICE_BOUND)
        kprintf("kbd: ready; key presses are logged (PgUp/PgDn/Home/End scroll the log)\n");
    if (mouse && mouse->state == DEVICE_BOUND) {
        kprintf("mouse: ready; pointer starts at the centre, clicks logged\n");
        log_pointer(x, y, buttons, 1);
    }

    /* The boot log stays up while booting (visible progress); then the
     * desktop takes over. The gfx self-test's pattern is left on screen for
     * its screenshot instead. */
#ifndef LKX_SELFTEST_GFX
    desktop_start();
#endif

    for (;;) {
        struct key_event keys[8];
        struct mouse_event moves[16];
        desktop_tick();         /* the clock: rtc0's IRQ 8 wakes the hlt once a second */
        /* Check for events with interrupts off, so one arriving between the
         * check and the hlt can't be missed: `sti; hlt` is atomic. */
        __asm__ volatile("cli");
        int nk = dev_read(kbd, keys, sizeof(keys));
        int nm = dev_read(mouse, moves, sizeof(moves));
        if (nk <= 0 && nm <= 0 && !moved) {
            __asm__ volatile("sti; hlt");
            continue;
        }
        __asm__ volatile("sti");

        for (int i = 0; i < nk / (int)sizeof(keys[0]); i++) {
            if (desktop_input_key(&keys[i]))
                continue;
            if (wm_is_open())
                wm_input_key(&keys[i]);
            else
                log_key(&keys[i]);
        }

        for (int i = 0; i < nm / (int)sizeof(moves[0]); i++) {
            x += moves[i].dx;
            y += moves[i].dy;
            x = x < 0 ? 0 : x >= w ? w - 1 : x;
            y = y < 0 ? 0 : y >= h ? h - 1 : y;
            /* Per packet, so no click is lost: the shell first (top bar,
             * menu, desktop), then the open window. */
            if (!desktop_input_mouse(x, y, moves[i].buttons) && wm_is_open())
                wm_input_mouse(x, y, moves[i].buttons);
            moved |= moves[i].dx || moves[i].dy;
            if (moves[i].buttons != buttons) {      /* clicks are always logged */
                buttons = moves[i].buttons;
                log_pointer(x, y, buttons, 1);
                moved = 0;
                last_log = uptime_ms();
            }
        }
        if (nm > 0)
            cursor_move_to(x, y);   /* once per batch: two small screen updates */
        desktop_handle_events();
        if (moved && uptime_ms() - last_log >= MOUSE_LOG_MS) {
            log_pointer(x, y, buttons, 0);
            moved = 0;
            last_log = uptime_ms();
        } else if (moved) {
            /* Movement not logged yet and no timer IRQ exists to wake us:
             * spin briefly instead of halting, so the final position of a
             * gesture still gets logged. */
            __asm__ volatile("pause");
        }
    }
}

/* How fast the display path is: write-combining makes every framebuffer
 * write roughly 10-30x cheaper on real hardware (kernel/mtrr.h). */
static void display_report(void)
{
    struct fb_info info;
    if (dev_ioctl(device_find("fb0"), FB_GET_INFO, &info) < 0)
        return;
    kprintf("fb0: write-combining %s", mtrr_result_name(info.wc_status));
    if (info.wc_status == MTRR_OK)
        kprintf(" (MTRR 0x%08x-0x%08x)", info.wc_base, info.wc_base + info.wc_len - 1);
    kprintf("\n");
}

void kmain(uint32_t magic, const struct boot_info *handoff)
{
    serial_init();
    kprintf("\nLiteKern X\n");

    const struct boot_info *bi = keep_boot_info(magic, handoff);
    status_init(bi);
    gdt_init();
    idt_init();
    irq_init();             /* PICs remapped, every line masked; interrupts stay off */
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

    /* First frame: the screen (back buffer), the on-screen log on it
     * (replaying everything so far) and the cursor. Without a display or
     * font, the plain status colour. */
    int screen_err = screen_init(device_find("fb0"));
    if (screen_err < 0 || console_init(bi->font_addr) < 0)
        kprintf("console: unavailable (%s); status colour only\n",
                screen_err < 0 ? "no screen" : "no BIOS font");
    status_show(STATUS_READY);
    if (screen_ready() && cursor_init() < 0)
        kprintf("cursor: no \"arrow\" cursor built in\n");
    uint64_t ready = rdtsc();
    boot_phase("first_frame", drivers_done, ready);

    /* Everything is reported after the fact, so no phase above includes the
     * time spent printing (slow over serial in the VMs). */
    boot_report(ready);
    memmap_report(bi);
    vbe_report(bi);
    vmm_report();
    pci_report();
    device_report();
    display_report();

#ifdef LKX_DIAG_VBIOS
    vbios_diag(bi);
#endif
#ifdef LKX_SELFTEST_DRIVERS
    selftest_drivers_run();
#endif
#ifdef LKX_SELFTEST_USER
    selftest_user_run();
#endif
#ifdef LKX_SELFTEST_GFX
    selftest_gfx_run();
#endif
#ifdef LKX_SELFTEST_WM
    selftest_wm_run();
#endif
#ifdef LKX_SELFTEST_WIDGETS
    selftest_widgets_run();
#endif
#ifdef LKX_SELFTEST_DISK
    selftest_disk_run();
#endif
#ifdef LKX_SELFTEST_KERNEL_NULL
    kprintf("selftest: kernel null-pointer write\n");
    *(volatile uint32_t *)0 = 1;
#endif
#ifdef LKX_SELFTEST_KERNEL_WP
    kprintf("selftest: kernel write to its own code\n");
    *(volatile uint8_t *)(uintptr_t)kmain = 0xcc;
#endif

    storage_init();         /* the disks Files can open (reads each partition table) */
    idle(bi);
}

/* LiteKern X — kernel entry point (called from kernel/entry.asm). */
#include "boot/bootinfo.h"
#include "drivers/builtin.h"
#include "kernel/acpi.h"
#include "kernel/apps.h"
#include "kernel/desktop.h"
#include "kernel/console.h"
#include "kernel/cursor.h"
#include "kernel/driver.h"
#include "kernel/screen.h"
#include "kernel/screensaver.h"
#include "kernel/fb.h"
#include "kernel/idle.h"
#include "kernel/lkx.h"
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
#include "kernel/splash.h"
#include "kernel/status.h"
#include "kernel/theme.h"
#include "defaults.h"
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

/* The input loop's state: the pointer is the kernel's, whoever runs the loop
 * (idle() between apps, SYS_WAIT_EVENT while an app waits). */
static struct {
    device_t *kbd, *mouse;
    int w, h, x, y, moved;
    uint8_t buttons;
    uint32_t last_log;
} in;

void idle_step(int busy)
{
    struct key_event keys[8];
    struct mouse_event moves[16];
    desktop_tick();         /* the clock: rtc0's IRQ 8 wakes the hlt once a second */
    screensaver_poll();     /* its next frame (rtc0 wakes the hlt 64 times a second then),
                             * or start it once the idle time is up */
    /* Check for events with interrupts off, so one arriving between the
     * check and the hlt can't be missed: `sti; hlt` is atomic. */
    __asm__ volatile("cli");
    int nk = dev_read(in.kbd, keys, sizeof(keys));
    int nm = dev_read(in.mouse, moves, sizeof(moves));
    if (nk <= 0 && nm <= 0 && !in.moved) {
        if (busy || (desktop_busy() && !screensaver_active()))
            __asm__ volatile("sti; pause");     /* an app ran or something moves: no sleeping */
        else
            __asm__ volatile("sti; hlt");
        return;
    }
    __asm__ volatile("sti");
    /* Any key or touch while the screen saver shows only wakes the screen. */
    if ((nk > 0 || nm > 0) && screensaver_input())
        return;

    /* After each key and click, the apps (and kernel apps) answer before the
     * next one is handled: what a click does can depend on what the one
     * before it did (a close button, then the dock icon of the same app). */
    for (int i = 0; i < nk / (int)sizeof(keys[0]); i++) {
        if (desktop_input_key(&keys[i]))
            continue;
        if (wm_focused())
            wm_input_key(&keys[i]);
        else
            log_key(&keys[i]);
        lkx_schedule();
        desktop_handle_events();
    }

    /* The cursor goes to where this batch ends first: it then keeps up with
     * the hand even when what the moves cause (a drag, hover highlights)
     * takes a while to draw. */
    if (nm > 0) {
        int x = in.x, y = in.y;
        for (int i = 0; i < nm / (int)sizeof(moves[0]); i++) {
            x += moves[i].dx;
            y += moves[i].dy;
            x = x < 0 ? 0 : x >= in.w ? in.w - 1 : x;
            y = y < 0 ? 0 : y >= in.h ? in.h - 1 : y;
        }
        cursor_move_to(x, y);
    }
    int npkt = nm / (int)sizeof(moves[0]);
    for (int i = 0; i < npkt; i++) {
        in.x += moves[i].dx;
        in.y += moves[i].dy;
        in.x = in.x < 0 ? 0 : in.x >= in.w ? in.w - 1 : in.x;
        in.y = in.y < 0 ? 0 : in.y >= in.h ? in.h - 1 : in.y;
        /* Moves with the same buttons as the next packet are merged into it:
         * only where the pointer ends up matters, and each step of a drag
         * costs a redraw the Atom can't keep up with at 200 a second. Every
         * button change still goes through on its own, so no click is lost.
         * The shell (top bar, menu, dock) passes what isn't its own to the
         * windows. */
        if (i + 1 < npkt && moves[i + 1].buttons == moves[i].buttons &&
            moves[i].buttons == in.buttons) {
            in.moved |= moves[i].dx || moves[i].dy;
            continue;
        }
        desktop_input_mouse(in.x, in.y, moves[i].buttons);
        in.moved |= moves[i].dx || moves[i].dy;
        if (moves[i].buttons != in.buttons) {       /* clicks are always logged */
            in.buttons = moves[i].buttons;
            log_pointer(in.x, in.y, in.buttons, 1);
            in.moved = 0;
            in.last_log = uptime_ms();
            lkx_schedule();
            desktop_handle_events();
        }
    }
    desktop_handle_events();
    shell_flush();          /* one redraw for the whole batch */
    if (in.moved && uptime_ms() - in.last_log >= MOUSE_LOG_MS) {
        log_pointer(in.x, in.y, in.buttons, 0);
        in.moved = 0;
        in.last_log = uptime_ms();
    } else if (in.moved) {
        /* Movement not logged yet and no timer IRQ exists to wake us:
         * spin briefly instead of halting, so the final position of a
         * gesture still gets logged. */
        __asm__ volatile("pause");
    }
}

static void __attribute__((noreturn)) idle(const struct boot_info *bi)
{
    in.kbd = device_find("kbd0");
    in.mouse = device_find("mouse0");
    in.w = (int)bi->fb_width;
    in.h = (int)bi->fb_height;
    in.x = in.w / 2;
    in.y = in.h / 2;

    if (in.kbd && in.kbd->state == DEVICE_BOUND)
        kprintf("kbd: ready; key presses are logged (PgUp/PgDn/Home/End scroll the log)\n");
    if (in.mouse && in.mouse->state == DEVICE_BOUND) {
        kprintf("mouse: ready; pointer starts at the centre, clicks logged\n");
        log_pointer(in.x, in.y, in.buttons, 1);
    }

    /* The boot log stays up while booting (visible progress); then the
     * desktop takes over. The gfx self-test's pattern is left on screen for
     * its screenshot instead. */
#ifndef LKX_SELFTEST_GFX
    desktop_start();
#endif
    for (;;) {
        int ran = lkx_schedule();       /* every app with an event, a turn each */
        idle_step(ran);
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
    /* The build-time look (make STYLE= ACCENT=), then the splash over the
     * log; the pointer appears with the desktop. */
    theme_set(theme_find_style(DEFAULT_STYLE), theme_find_accent(DEFAULT_ACCENT));
    splash_start();
    if (screen_ready() && !splash_active() && cursor_init() < 0)
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
    splash_progress(30);

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

    acpi_init();            /* power-off: the firmware's tables (before any app's address space) */
    storage_init();         /* the disks Files can open (reads each partition table) */
    splash_progress(45);
    idle(bi);
}

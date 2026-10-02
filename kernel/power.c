#include "kernel/power.h"
#include "drivers/i8042.h"
#include "kernel/acpi.h"
#include "kernel/io.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/text.h"
#include "kernel/timing.h"

#define I8042_CMD_RESET 0xfe    /* pulse the CPU reset line */

void power_restart(void)
{
    kprintf("power: restarting\n");
    __asm__ volatile("cli");
    for (int i = 0; i < 100000 && (inb(I8042_STATUS) & I8042_STATUS_IN); i++)
        ;
    outb(I8042_STATUS, I8042_CMD_RESET);    /* port 0x64 is also the command port */
    for (volatile int i = 0; i < 10000000; i++)
        ;
    /* Still here: an empty IDT makes the next exception a triple fault,
     * which resets the CPU. */
    static const struct __attribute__((packed)) { uint16_t limit; uint32_t base; } none = { 0, 0 };
    __asm__ volatile("lidt %0; int3" : : "m"(none));
    for (;;)
        __asm__ volatile("hlt");
}

/* Running under a hypervisor (CPUID leaf 1, ECX bit 31)? Then the virtual
 * machines' own power-off ports are safe to try; on real hardware we never
 * write to ports we haven't been told about. */
static int in_vm(void)
{
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (c >> 31) & 1;
}

int power_can_off(void)
{
    return acpi_can_power_off() || in_vm();
}

void power_off(void)
{
    kprintf("power: switching off (%s)\n", acpi_can_power_off() ? "ACPI S5" : "virtual machine port");
    acpi_power_off();
    if (in_vm()) {
        outw(0x604, 0x2000);        /* QEMU (PIIX4 / ICH9) */
        outw(0xb004, 0x2000);       /* Bochs, older QEMU */
        outw(0x4004, 0x3400);       /* VirtualBox */
        uint32_t t0 = uptime_ms();
        while (uptime_ms() - t0 < 500)
            __asm__ volatile("pause");
    }
    /* Still on: say so, and stop. */
    kprintf("power: still on; it's safe to switch off now\n");
    __asm__ volatile("cli");
    if (screen_ready()) {
        struct gfx_surface *s = screen_surface();
        screen_set_overlay(0, 0, 0, 0, 0);     /* no pointer either */
        const char *msg = "It's now safe to switch off your computer.";
        gfx_fill_rect(s, 0, 0, s->w, s->h, 0);
        text_draw(s, (s->w - text_width(msg, TEXT_HEADING)) / 2, s->h / 2 - 12, msg, TEXT_HEADING,
                  0xffffff);
        screen_damage_all();
        screen_present();
    }
    for (;;)
        __asm__ volatile("hlt");
}

#include "kernel/power.h"
#include "drivers/i8042.h"
#include "kernel/io.h"
#include "kernel/printk.h"

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

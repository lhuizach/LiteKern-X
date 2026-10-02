/* LiteKern X — restarting and switching off (the top bar's power menu).
 *
 * Switching off uses ACPI's S5 state (kernel/acpi.h; Phase 5 §1's first
 * piece, brought forward). FAT32 writes go straight to the disk, so there's
 * nothing to flush first. */
#ifndef LKX_POWER_H
#define LKX_POWER_H

/* Reset the machine: the keyboard controller's reset line (as the BIOS and
 * Linux do), and if that doesn't take, a triple fault. Never returns. */
void power_restart(void) __attribute__((noreturn));

/* 1 if power_off() can switch the machine off. */
int power_can_off(void);

/* Switch off: ACPI S5; in a virtual machine, its own power-off port if that
 * didn't work. If the machine is still on after all that, show "safe to
 * turn off" and halt. Never returns. */
void power_off(void) __attribute__((noreturn));

#endif

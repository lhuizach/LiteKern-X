/* LiteKern X — restarting (the top bar's power menu).
 *
 * Shutting down needs ACPI, which is Phase 5 (05-LiteKernX-Expansion.md);
 * restarting doesn't. */
#ifndef LKX_POWER_H
#define LKX_POWER_H

/* Reset the machine: the keyboard controller's reset line (as the BIOS and
 * Linux do), and if that doesn't take, a triple fault. Never returns. */
void power_restart(void) __attribute__((noreturn));

#endif

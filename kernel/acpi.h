/* LiteKern X — just enough ACPI to switch the machine off (Phase 5 §1,
 * brought forward on the user's request, 2026-10-02).
 *
 * At boot it finds the firmware's tables (RSDP -> RSDT -> FADT -> DSDT) and
 * reads two things: the PM1 control registers (FADT) and the sleep-type
 * values of the _S5 ("soft off") object in the DSDT. That's a tiny AML
 * reader for one object, not an interpreter: _S5 is a plain package of
 * numbers on every machine we've seen (QEMU, VirtualBox, Intel ICH7 boards
 * like the EeePC's). Power-off then writes SLP_TYPx | SLP_EN to PM1a (and
 * PM1b). */
#ifndef LKX_ACPI_H
#define LKX_ACPI_H

/* Find the tables. Logs what it found ("acpi: ..."). 0 or -ENODEV. */
int acpi_init(void);

/* 1 if acpi_init found everything power-off needs. */
int acpi_can_power_off(void);

/* Enter S5 (soft off). Returns only if the machine is still on after it. */
void acpi_power_off(void);

#endif

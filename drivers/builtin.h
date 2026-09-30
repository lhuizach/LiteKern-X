/* LiteKern X — the drivers built into the kernel, and the legacy devices they
 * drive. Adding a driver: implement a driver_t in drivers/, declare it here,
 * add it to drivers_register(). */
#ifndef LKX_DRIVERS_BUILTIN_H
#define LKX_DRIVERS_BUILTIN_H

#include "boot/bootinfo.h"
#include "kernel/driver.h"

extern const driver_t uart_driver;
extern const driver_t vbefb_driver;
extern const driver_t kbd_driver;
extern const driver_t mouse_driver;
extern const driver_t rtc_driver;
extern const driver_t bios_disk_driver;
extern const driver_t ata_driver;

/* Register every built-in driver (before any binding happens). */
void drivers_register(void);

/* Create and bind the legacy (non-PCI) devices. */
void drivers_add_legacy_devices(const struct boot_info *bi);

#endif

#include "drivers/builtin.h"

void drivers_register(void)
{
    driver_add(&uart_driver);
    driver_add(&vbefb_driver);
    driver_add(&kbd_driver);
    driver_add(&mouse_driver);
    driver_add(&rtc_driver);
    driver_add(&bios_disk_driver);
    driver_add(&ata_driver);
}

void drivers_add_legacy_devices(const struct boot_info *bi)
{
    device_add_legacy(&uart_driver, "com1", 0x3f8);
    device_add_legacy(&vbefb_driver, "fb0", (uintptr_t)bi);
    device_add_legacy(&kbd_driver, "kbd0", 0);
    device_add_legacy(&mouse_driver, "mouse0", 0);
    device_add_legacy(&rtc_driver, "rtc0", 0);
    device_add_legacy(&bios_disk_driver, "boot0", bi->boot_drive);
    device_add_legacy(&ata_driver, "ata0", 0);      /* after boot0: it compares with it */
}

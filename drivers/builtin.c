#include "drivers/builtin.h"

void drivers_register(void)
{
    driver_add(&uart_driver);
    driver_add(&vbefb_driver);
}

void drivers_add_legacy_devices(const struct boot_info *bi)
{
    device_add_legacy(&uart_driver, "com1", 0x3f8);
    device_add_legacy(&vbefb_driver, "fb0", (uintptr_t)bi);
}

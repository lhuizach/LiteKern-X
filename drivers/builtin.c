#include "drivers/builtin.h"

void drivers_register(void)
{
    driver_add(&uart_driver);
}

void drivers_add_legacy_devices(void)
{
    device_add_legacy(&uart_driver, "com1", 0x3f8);
}

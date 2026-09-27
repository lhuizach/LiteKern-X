/* LiteKern X — driver-layer self-test. Linked into test builds only
 * (EXTRA_KERNEL_SRCS, -DLKX_SELFTEST_DRIVERS); see tests/kernel/test-kernel.sh.
 *
 * Registers fake drivers against QEMU's PCI devices and checks the driver
 * layer's promises from kernel/driver.h. Prints one "selftest: ok|FAIL ..."
 * line per check and a summary. With -DLKX_SELFTEST_BAD_DRIVER it also
 * registers a driver with a missing operation, which must panic. */
#include "drivers/builtin.h"
#include "kernel/driver.h"
#include "kernel/errno.h"
#include "kernel/printk.h"

static device_t *com3;

static int ide_inits, ide_shutdowns;
static device_t *ide_dev;

static int ide_init(device_t *dev)
{
    ide_inits++;
    ide_dev = dev;
    return 0;
}

static void ide_shutdown(device_t *dev)
{
    (void)dev;
    ide_shutdowns++;
}

static const struct pci_id ide_ids[] = {
    { 0x8086, 0x7010 },     /* QEMU's PIIX3 IDE controller */
    { 0, 0 },
};

static const driver_t selftest_ide = {
    .name = "selftest-ide",
    .pci_ids = ide_ids,
    .init = ide_init,
    .read = driver_nosys_read,
    .write = driver_nosys_write,
    .ioctl = driver_nosys_ioctl,
    .shutdown = ide_shutdown,
};

static int fail_init(device_t *dev)
{
    (void)dev;
    return -EIO;
}

static const struct pci_id fail_ids[] = {
    { 0x1234, PCI_ANY_ID },  /* QEMU's VGA (1234:1111), by vendor wildcard */
    { 0, 0 },
};

static const driver_t selftest_fail = {
    .name = "selftest-fail",
    .pci_ids = fail_ids,
    .init = fail_init,
    .read = driver_nosys_read,
    .write = driver_nosys_write,
    .ioctl = driver_nosys_ioctl,
    .shutdown = driver_noop_shutdown,
};

#ifdef LKX_SELFTEST_BAD_DRIVER
static const driver_t selftest_bad = {
    .name = "selftest-bad",
    .init = fail_init,
    .read = NULL,           /* must be refused */
    .write = driver_nosys_write,
    .ioctl = driver_nosys_ioctl,
    .shutdown = driver_noop_shutdown,
};
#endif

void selftest_drivers_register(void)
{
    driver_add(&selftest_ide);
    driver_add(&selftest_fail);
#ifdef LKX_SELFTEST_BAD_DRIVER
    driver_add(&selftest_bad);
#endif
    /* Nothing decodes COM3 in the QEMU VM: the same situation as COM1 on
     * the EeePC, which has no UART at all. */
    com3 = device_add_legacy(&uart_driver, "com3", 0x3e8);
}

static int passed, failed;

static void check(int ok, const char *what)
{
    kprintf("selftest: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

void selftest_drivers_run(void)
{
    static const char msg[] = "selftest: hello through the com1 driver\r\n";
    char buf[4];

    device_t *com1 = device_find("com1");
    check(com1 && com1->state == DEVICE_BOUND, "com1 is bound to the uart driver");
    check(dev_write(com1, msg, sizeof(msg) - 1) == (int)sizeof(msg) - 1,
          "dev_write reaches the UART through the driver");
    check(dev_read(com1, buf, sizeof(buf)) == 0, "dev_read with no input returns 0 (non-blocking)");
    check(dev_ioctl(com1, 1, NULL) == -ENOSYS, "unsupported ioctl returns -ENOSYS");
    check(com3 && com3->state == DEVICE_FAILED && com3->error == -ENODEV,
          "a UART where no hardware exists fails init with -ENODEV");
    check(dev_write(com3, "x", 1) == -ENODEV, "writes to the missing UART are refused, not dropped");

    check(ide_inits == 1, "PCI match by vendor:device ran init() exactly once");
    check(ide_dev && ide_dev->pci && ide_dev->pci->bus == 0 && ide_dev->pci->dev == 1 &&
              ide_dev->pci->func == 1,
          "the bound device carries its PCI location (00:01.1)");
    check(device_find("pci 00:01.1") == ide_dev, "device_find by PCI name");
    check(dev_read(ide_dev, buf, sizeof(buf)) == -ENOSYS, "driver_nosys_read returns -ENOSYS");

    device_t *vga = device_find("pci 00:02.0");
    check(vga && vga->drv == &selftest_fail, "PCI_ANY_ID matches any device of the vendor");
    check(vga && vga->state == DEVICE_FAILED && vga->error == -EIO,
          "init() failure marks the device FAILED with its error");
    check(dev_write(vga, "x", 1) == -ENODEV, "calls on a FAILED device return -ENODEV");

    dev_shutdown(ide_dev);
    check(ide_shutdowns == 1 && ide_dev->state == DEVICE_OFF, "dev_shutdown runs shutdown() once");
    dev_shutdown(ide_dev);
    check(ide_shutdowns == 1, "a second dev_shutdown is a no-op");
    check(dev_read(ide_dev, buf, sizeof(buf)) == -ENODEV, "calls on an OFF device return -ENODEV");

    check(device_find("no-such-device") == NULL, "device_find on an unknown name returns NULL");
    check(dev_write(NULL, "x", 1) == -ENODEV, "calls on a NULL device return -ENODEV");

    kprintf("selftest: drivers %d/%d passed\n", passed, passed + failed);
}

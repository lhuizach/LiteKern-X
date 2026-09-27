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
#include "kernel/fb.h"
#include "kernel/input.h"
#include "kernel/irq.h"
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

/* Never runs: only used to try registering IRQ handlers that must be refused. */
static void ide_shutdown_irq(void *ctx)
{
    (void)ctx;
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

    /* Display driver (drivers/vbefb.c). Draws in the corners of the screen and
     * reads the pixels back through the kernel's mapping of the framebuffer. */
    device_t *fb = device_find("fb0");
    struct fb_info info = { 0 };
    check(fb && fb->state == DEVICE_BOUND, "fb0 is bound to the vbefb driver");
    check(dev_ioctl(fb, FB_GET_INFO, &info) == 0 && info.width && info.height && info.bpp == 32 &&
              info.pitch >= info.width * 4,
          "FB_GET_INFO describes a 32 bpp mode");
    uint32_t w = info.width, h = info.height;
#define PIXEL(x, y) (((volatile uint32_t *)(info.phys_addr + (y) * info.pitch))[x])

    struct fb_rect r1 = { 0, 0, 8, 8, 0x00123456 };
    check(dev_ioctl(fb, FB_FILL_RECT, &r1) == 0 && PIXEL(3, 3) == 0x00123456 &&
              PIXEL(8, 8) != 0x00123456,
          "FB_FILL_RECT fills exactly the rectangle");
    struct fb_rect r2 = { w - 4, h - 4, 50, 50, 0x00654321 };
    check(dev_ioctl(fb, FB_FILL_RECT, &r2) == 0 && PIXEL(w - 1, h - 1) == 0x00654321,
          "FB_FILL_RECT clips a rectangle that runs off the screen");
    struct fb_rect r3 = { w + 10, 0, 5, 5, 0x00ffffff };
    check(dev_ioctl(fb, FB_FILL_RECT, &r3) == 0, "a rectangle entirely off-screen draws nothing");

    static const uint32_t pattern[] = { 0x00010203, 0x00040506, 0x00070809, 0x000a0b0c };
    struct fb_blit b1 = { 20, 0, 2, 2, pattern, 2 };
    check(dev_ioctl(fb, FB_BLIT, &b1) == 0 && PIXEL(20, 0) == pattern[0] &&
              PIXEL(21, 0) == pattern[1] && PIXEL(20, 1) == pattern[2] && PIXEL(21, 1) == pattern[3],
          "FB_BLIT copies pixels row by row");
    struct fb_blit b2 = { 0, 0, 2, 2, NULL, 2 };
    struct fb_blit b3 = { 0, 0, 2, 2, pattern, 1 };
    check(dev_ioctl(fb, FB_BLIT, &b2) == -EINVAL && dev_ioctl(fb, FB_BLIT, &b3) == -EINVAL,
          "FB_BLIT refuses a NULL source or a stride shorter than the width");
    check(dev_ioctl(fb, 99, &info) == -ENOSYS && dev_ioctl(fb, FB_GET_INFO, NULL) == -EINVAL,
          "unknown ioctl -> -ENOSYS, NULL argument -> -EINVAL");
    check(dev_write(fb, "x", 1) == -ENOSYS, "the framebuffer is not a byte stream (write -> -ENOSYS)");
#undef PIXEL

    /* Keyboard driver (drivers/kbd.c) and IRQ lines. Key decoding itself is
     * tested by typing into the VM (tests/kernel/test-kernel.sh). */
    device_t *kbd = device_find("kbd0");
    struct key_event ev[2];
    uint32_t dropped = 1;
    check(kbd && kbd->state == DEVICE_BOUND, "kbd0 is bound to the ps2kbd driver");
    check(dev_read(kbd, ev, sizeof(ev[0]) + 1) == -EINVAL, "reading part of a key event -> -EINVAL");
    check(dev_read(kbd, ev, sizeof(ev)) == 0, "reading with no keys pressed returns 0 (non-blocking)");
    check(dev_ioctl(kbd, KBD_GET_DROPPED, &dropped) == 0 && dropped == 0, "no key events dropped");
    check(dev_write(kbd, "x", 1) == -ENOSYS, "keyboard write -> -ENOSYS");
    check(irq_register(1, ide_shutdown_irq, 0) == -EBUSY, "a second handler on IRQ 1 -> -EBUSY");
    check(irq_register(2, ide_shutdown_irq, 0) == -EINVAL && irq_register(16, ide_shutdown_irq, 0) == -EINVAL,
          "IRQ 2 (cascade) and IRQ 16 can't be registered");

    kprintf("selftest: drivers %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("driver self-test: %d checks failed", failed);
}

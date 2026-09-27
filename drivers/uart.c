/* LiteKern X — 16550 UART driver: the reference driver for the driver layer
 * (Phase 1 §4). Legacy device: legacy_arg is the I/O port base (COM1 = 0x3f8).
 *
 *   read   non-blocking: returns the bytes waiting in the receive FIFO (maybe 0)
 *   write  blocking, with a bounded wait per byte (-EIO if the UART stalls)
 *   ioctl  not supported (-ENOSYS)
 *
 * The EeePC 1000HE has no UART, so init() fails there with -ENODEV — which is
 * the point: the device is reported FAILED instead of silently eating writes.
 * (printk() keeps its own direct COM1 path: it must work before drivers exist
 * and during a panic.) */
#include "drivers/builtin.h"
#include "kernel/errno.h"
#include "kernel/io.h"

#define REG_DATA        0
#define REG_IER         1
#define REG_FCR         2
#define REG_LCR         3
#define REG_LSR         5
#define REG_SCRATCH     7
#define LSR_DATA_READY  0x01
#define LSR_THR_EMPTY   0x20
#define TX_SPINS        100000

static uint16_t port(const device_t *dev, uint16_t reg)
{
    return (uint16_t)dev->legacy_arg + reg;
}

static int uart_init(device_t *dev)
{
    /* No UART: nothing decodes the port, so the scratch register can't hold a value. */
    outb(port(dev, REG_SCRATCH), 0x5a);
    if (inb(port(dev, REG_SCRATCH)) != 0x5a)
        return -ENODEV;
    outb(port(dev, REG_SCRATCH), 0xa5);
    if (inb(port(dev, REG_SCRATCH)) != 0xa5)
        return -ENODEV;

    outb(port(dev, REG_IER), 0x00);     /* polled: no interrupts */
    outb(port(dev, REG_LCR), 0x80);     /* DLAB on */
    outb(port(dev, REG_DATA), 0x01);    /* divisor 1: 115200 baud */
    outb(port(dev, REG_IER), 0x00);
    outb(port(dev, REG_LCR), 0x03);     /* 8N1, DLAB off */
    outb(port(dev, REG_FCR), 0xc7);     /* FIFOs on, cleared */
    return 0;
}

static int uart_read(device_t *dev, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t n = 0;
    while (n < len && (inb(port(dev, REG_LSR)) & LSR_DATA_READY))
        p[n++] = inb(port(dev, REG_DATA));
    return (int)n;
}

static int uart_write(device_t *dev, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    for (size_t n = 0; n < len; n++) {
        int spins = 0;
        while (!(inb(port(dev, REG_LSR)) & LSR_THR_EMPTY))
            if (++spins > TX_SPINS)
                return -EIO;
        outb(port(dev, REG_DATA), p[n]);
    }
    return (int)len;
}

const driver_t uart_driver = {
    .name = "uart16550",
    .pci_ids = NULL,
    .init = uart_init,
    .read = uart_read,
    .write = uart_write,
    .ioctl = driver_nosys_ioctl,
    .shutdown = driver_noop_shutdown,
};

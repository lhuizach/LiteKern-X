#include "kernel/driver.h"
#include "kernel/errno.h"
#include "kernel/printk.h"

#define MAX_DRIVERS 16
#define MAX_DEVICES 32

static const driver_t *drivers[MAX_DRIVERS];
static uint32_t driver_count;

static struct device devices[MAX_DEVICES];
static uint32_t device_count;

int driver_nosys_read(device_t *dev, void *buf, size_t len)
{
    (void)dev, (void)buf, (void)len;
    return -ENOSYS;
}

int driver_nosys_write(device_t *dev, const void *buf, size_t len)
{
    (void)dev, (void)buf, (void)len;
    return -ENOSYS;
}

int driver_nosys_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev, (void)cmd, (void)arg;
    return -ENOSYS;
}

void driver_noop_shutdown(device_t *dev)
{
    (void)dev;
}

static void check_driver(const driver_t *drv)
{
    if (!drv || !drv->name)
        panic("driver_add: driver without a name");
    const char *missing = !drv->init     ? "init"
                        : !drv->read     ? "read"
                        : !drv->write    ? "write"
                        : !drv->ioctl    ? "ioctl"
                        : !drv->shutdown ? "shutdown"
                        : NULL;
    if (missing)
        panic("driver %s: missing operation '%s' (use the driver_nosys_* helpers)",
              drv->name, missing);
}

void driver_add(const driver_t *drv)
{
    check_driver(drv);
    for (uint32_t i = 0; i < driver_count; i++)
        if (drivers[i] == drv)
            return;
    if (driver_count == MAX_DRIVERS)
        panic("driver %s: driver table full (%u)", drv->name, MAX_DRIVERS);
    drivers[driver_count++] = drv;
}

static void copy_name(char *dst, const char *src)
{
    size_t i = 0;
    for (; src[i] && i < sizeof(((struct device *)0)->name) - 1; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static device_t *new_device(const driver_t *drv, const char *name)
{
    if (device_count == MAX_DEVICES)
        panic("device %s: device table full (%u)", name, MAX_DEVICES);
    device_t *dev = &devices[device_count++];
    copy_name(dev->name, name);
    dev->drv = drv;
    return dev;
}

static void bind(device_t *dev)
{
    int err = dev->drv->init(dev);
    if (err < 0) {
        dev->state = DEVICE_FAILED;
        dev->error = err;
    } else {
        dev->state = DEVICE_BOUND;
    }
}

device_t *device_add_legacy(const driver_t *drv, const char *name, uintptr_t arg)
{
    driver_add(drv);
    device_t *dev = new_device(drv, name);
    dev->legacy_arg = arg;
    bind(dev);
    return dev;
}

static int matches(const driver_t *drv, const struct pci_device *pci)
{
    if (!drv->pci_ids)
        return 0;
    for (const struct pci_id *id = drv->pci_ids; id->vendor_id; id++)
        if (id->vendor_id == pci->vendor_id &&
            (id->device_id == PCI_ANY_ID || id->device_id == pci->device_id))
            return 1;
    return 0;
}

static void hex2(char *p, uint8_t v)
{
    p[0] = "0123456789abcdef"[v >> 4];
    p[1] = "0123456789abcdef"[v & 15];
}

void driver_probe_pci(void)
{
    for (uint32_t i = 0; i < pci_device_count; i++) {
        const struct pci_device *pci = &pci_devices[i];
        for (uint32_t d = 0; d < driver_count; d++) {
            if (!matches(drivers[d], pci))
                continue;
            char name[] = "pci BB:DD.F";
            hex2(name + 4, pci->bus);
            hex2(name + 7, pci->dev);
            name[10] = '0' + pci->func;
            device_t *dev = new_device(drivers[d], name);
            dev->pci = pci;
            bind(dev);
            break;
        }
    }
}

device_t *device_find(const char *name)
{
    for (uint32_t i = 0; i < device_count; i++) {
        const char *a = devices[i].name, *b = name;
        while (*a && *a == *b)
            a++, b++;
        if (*a == *b)
            return &devices[i];
    }
    return NULL;
}

int dev_read(device_t *dev, void *buf, size_t len)
{
    if (!dev || dev->state != DEVICE_BOUND)
        return -ENODEV;
    return dev->drv->read(dev, buf, len);
}

int dev_write(device_t *dev, const void *buf, size_t len)
{
    if (!dev || dev->state != DEVICE_BOUND)
        return -ENODEV;
    return dev->drv->write(dev, buf, len);
}

int dev_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    if (!dev || dev->state != DEVICE_BOUND)
        return -ENODEV;
    return dev->drv->ioctl(dev, cmd, arg);
}

void dev_shutdown(device_t *dev)
{
    if (!dev || dev->state != DEVICE_BOUND)
        return;
    dev->drv->shutdown(dev);
    dev->state = DEVICE_OFF;
}

void device_report(void)
{
    uint32_t bound = 0, failed = 0;
    for (uint32_t i = 0; i < device_count; i++) {
        const device_t *dev = &devices[i];
        kprintf("dev %s driver=%s ", dev->name, dev->drv->name);
        switch (dev->state) {
        case DEVICE_BOUND:
            kprintf("bound\n");
            bound++;
            break;
        case DEVICE_FAILED:
            kprintf("FAILED (%s)\n", errno_name(dev->error));
            failed++;
            break;
        case DEVICE_OFF:
            kprintf("off\n");
            break;
        }
    }
    uint32_t pci_bound = 0;
    for (uint32_t i = 0; i < device_count; i++)
        if (devices[i].pci)
            pci_bound++;
    kprintf("drivers: %u registered, %u devices bound, %u failed; %u PCI devices without a driver\n",
            driver_count, bound, failed, pci_device_count - pci_bound);
}

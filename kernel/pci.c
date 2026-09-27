#include "kernel/pci.h"
#include "kernel/io.h"
#include "kernel/printk.h"

#define PCI_CONFIG_ADDRESS 0xcf8
#define PCI_CONFIG_DATA    0xcfc

struct pci_device pci_devices[PCI_MAX_DEVICES];
uint32_t pci_device_count;

static uint8_t bus_seen[256 / 8];
static uint32_t bus_count, dropped;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, 0x80000000u | (uint32_t)bus << 16 | (uint32_t)dev << 11 |
                                 (uint32_t)func << 8 | (offset & 0xfc));
    return inl(PCI_CONFIG_DATA);
}

static const char *class_name(uint8_t cls, uint8_t sub)
{
    static const struct { uint8_t cls, sub; const char *name; } names[] = {
        { 0x01, 0x01, "IDE controller" },
        { 0x01, 0x06, "SATA controller" },
        { 0x01, 0xff, "storage controller" },
        { 0x02, 0x00, "Ethernet controller" },
        { 0x02, 0xff, "network controller" },
        { 0x03, 0x00, "VGA controller" },
        { 0x03, 0xff, "display controller" },
        { 0x04, 0x01, "audio controller" },
        { 0x04, 0x03, "HD audio controller" },
        { 0x04, 0xff, "multimedia controller" },
        { 0x05, 0xff, "memory controller" },
        { 0x06, 0x00, "host bridge" },
        { 0x06, 0x01, "ISA bridge" },
        { 0x06, 0x04, "PCI bridge" },
        { 0x06, 0xff, "bridge" },
        { 0x07, 0xff, "communication controller" },
        { 0x08, 0xff, "system peripheral" },
        { 0x0c, 0x03, "USB controller" },
        { 0x0c, 0x05, "SMBus controller" },
        { 0x0c, 0xff, "serial bus controller" },
        { 0x0d, 0xff, "wireless controller" },
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (names[i].cls == cls && (names[i].sub == sub || names[i].sub == 0xff))
            return names[i].name;
    return "unknown device";
}

static void scan_bus(uint8_t bus);

static void scan_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t id = pci_read32(bus, dev, func, 0x00);
    if ((id & 0xffff) == 0xffff)
        return;
    uint32_t cls = pci_read32(bus, dev, func, 0x08);
    uint8_t header = (pci_read32(bus, dev, func, 0x0c) >> 16) & 0x7f;

    struct pci_device d = {
        .bus = bus, .dev = dev, .func = func,
        .header_type = header,
        .vendor_id = id & 0xffff, .device_id = id >> 16,
        .class_code = cls >> 24, .subclass = (cls >> 16) & 0xff,
        .prog_if = (cls >> 8) & 0xff, .revision = cls & 0xff,
    };
    if (pci_device_count < PCI_MAX_DEVICES)
        pci_devices[pci_device_count++] = d;
    else
        dropped++;

    /* PCI-to-PCI bridge: everything behind it lives on its secondary bus. */
    if (d.class_code == 0x06 && d.subclass == 0x04 && header == 0x01) {
        uint8_t secondary = (pci_read32(bus, dev, func, 0x18) >> 8) & 0xff;
        if (secondary != 0)
            scan_bus(secondary);
    }
}

static void scan_bus(uint8_t bus)
{
    if (bus_seen[bus / 8] & (1u << (bus % 8)))
        return;                     /* misconfigured bridges could loop */
    bus_seen[bus / 8] |= 1u << (bus % 8);
    bus_count++;

    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read32(bus, dev, 0, 0x00);
        if ((id & 0xffff) == 0xffff)
            continue;
        scan_function(bus, dev, 0);
        uint8_t header = pci_read32(bus, dev, 0, 0x0c) >> 16;
        if (header & 0x80)          /* multi-function device */
            for (uint8_t func = 1; func < 8; func++)
                scan_function(bus, dev, func);
    }
}

void pci_scan(void)
{
    /* Mechanism #1 is present if the address register reads back. */
    outl(PCI_CONFIG_ADDRESS, 0x80000000u);
    if (inl(PCI_CONFIG_ADDRESS) != 0x80000000u)
        panic("PCI configuration mechanism #1 not available");

    /* A multi-function host bridge means one root bus per function. */
    uint8_t host_header = pci_read32(0, 0, 0, 0x0c) >> 16;
    if (host_header & 0x80) {
        for (uint8_t func = 0; func < 8; func++)
            if ((pci_read32(0, 0, func, 0x00) & 0xffff) != 0xffff)
                scan_bus(func);
    } else {
        scan_bus(0);
    }
}

void pci_report(void)
{
    for (uint32_t i = 0; i < pci_device_count; i++) {
        const struct pci_device *d = &pci_devices[i];
        kprintf("pci %02x:%02x.%u %04x:%04x class %02x.%02x.%02x rev %02x %s\n",
                d->bus, d->dev, d->func, d->vendor_id, d->device_id, d->class_code,
                d->subclass, d->prog_if, d->revision, class_name(d->class_code, d->subclass));
    }
    kprintf("pci: %u devices on %u buses\n", pci_device_count + dropped, bus_count);
    if (dropped)
        kprintf("pci: warning: table full, %u devices not recorded\n", dropped);
}

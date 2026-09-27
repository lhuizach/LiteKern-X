/* LiteKern X — PCI bus enumeration (Phase 1 §3).
 *
 * Walks every bus reachable from the host bridge through PCI-to-PCI bridges
 * (on the EeePC, the Ethernet and Wi-Fi sit behind PCIe root ports) using
 * configuration mechanism #1. Only discovers and records devices: nothing is
 * initialised here. The table feeds driver matching (Phase 1 §4). */
#ifndef LKX_PCI_H
#define LKX_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 64

struct pci_device {
    uint8_t bus, dev, func;
    uint8_t header_type;        /* without the multi-function bit */
    uint16_t vendor_id, device_id;
    uint8_t class_code, subclass, prog_if, revision;
};

extern struct pci_device pci_devices[PCI_MAX_DEVICES];
extern uint32_t pci_device_count;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

/* Enumerate every device into pci_devices[], silently (so boot timing measures
 * the scan, not the logging). Panics if PCI config space is missing. */
void pci_scan(void);

/* Log the table built by pci_scan(). */
void pci_report(void);

#endif

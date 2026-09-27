/* LiteKern X — driver abstraction layer (Phase 1 §4).
 *
 * Rules, from the roadmap:
 *   - The kernel only ever talks to hardware drivers through this interface
 *     (dev_read / dev_write / dev_ioctl / dev_shutdown), never their internals.
 *   - Every operation must be implemented. An operation a driver doesn't
 *     support uses the driver_nosys_* / driver_noop_shutdown helpers, which
 *     return -ENOSYS: a NULL slot is a bug and driver_add() panics on it.
 *   - init() failing is loud: the device is marked FAILED with the error,
 *     reported at boot, and every later call on it returns -ENODEV. There
 *     are no half-bound devices.
 *
 * Devices come from two places: PCI devices found by pci_scan() and bound by
 * vendor/device ID (driver_probe_pci), and legacy devices with no PCI
 * identity (COM1, the i8042, the VBE framebuffer) added explicitly
 * (device_add_legacy). */
#ifndef LKX_DRIVER_H
#define LKX_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/pci.h"

#define PCI_ANY_ID 0xffff

/* One entry of a driver's PCI match list; the list ends with { 0, 0 }. */
struct pci_id {
    uint16_t vendor_id;
    uint16_t device_id;         /* PCI_ANY_ID matches any device of the vendor */
};

typedef struct device device_t;

typedef struct driver {
    const char *name;
    const struct pci_id *pci_ids;   /* NULL for legacy-only drivers */
    int  (*init)(device_t *dev);
    int  (*read)(device_t *dev, void *buf, size_t len);
    int  (*write)(device_t *dev, const void *buf, size_t len);
    int  (*ioctl)(device_t *dev, unsigned cmd, void *arg);
    void (*shutdown)(device_t *dev);
} driver_t;

enum device_state {
    DEVICE_BOUND,       /* init() succeeded; calls go through */
    DEVICE_FAILED,      /* init() failed; `error` says why */
    DEVICE_OFF,         /* shut down */
};

struct device {
    char name[16];                  /* "com1", "pci 00:1f.2" */
    const driver_t *drv;
    const struct pci_device *pci;   /* NULL for legacy devices */
    uintptr_t legacy_arg;           /* legacy devices: e.g. the I/O port base */
    void *priv;                     /* driver-owned */
    enum device_state state;
    int error;                      /* negative errno when FAILED */
};

/* Fillers for operations a driver doesn't support. */
int driver_nosys_read(device_t *dev, void *buf, size_t len);
int driver_nosys_write(device_t *dev, const void *buf, size_t len);
int driver_nosys_ioctl(device_t *dev, unsigned cmd, void *arg);
void driver_noop_shutdown(device_t *dev);

/* Register a driver. Panics if an operation is missing or the table is full. */
void driver_add(const driver_t *drv);

/* Create a legacy device, bind `drv` to it, and run init(). Returns the
 * device whatever the outcome; check its state. */
device_t *device_add_legacy(const driver_t *drv, const char *name, uintptr_t arg);

/* Bind registered drivers to pci_devices[] by vendor/device ID. The first
 * matching driver wins. PCI devices without a driver are left alone. */
void driver_probe_pci(void);

device_t *device_find(const char *name);

/* The only way the rest of the kernel reaches a driver. On a device that
 * isn't BOUND they return -ENODEV (dev_shutdown does nothing). */
int dev_read(device_t *dev, void *buf, size_t len);
int dev_write(device_t *dev, const void *buf, size_t len);
int dev_ioctl(device_t *dev, unsigned cmd, void *arg);
void dev_shutdown(device_t *dev);

/* Log every device with its driver and state, plus a summary. */
void device_report(void);

#endif

/* LiteKern X — i8042 PS/2 controller helpers, shared by the keyboard driver
 * (port 1, IRQ 1) and the touchpad/mouse driver (port 2, IRQ 12).
 *
 * The controller isn't a driver_t device of its own: it's the bus the two
 * PS/2 devices sit on. All waits are bounded, and nothing resets a device —
 * the BIOS has already brought them up, and PS/2 resets cost hundreds of ms
 * (docs/BOOT-BUDGET.md). */
#ifndef LKX_I8042_H
#define LKX_I8042_H

#include <stdint.h>

#define I8042_DATA          0x60
#define I8042_STATUS        0x64
#define I8042_STATUS_OUT    0x01    /* a byte is waiting at I8042_DATA */
#define I8042_STATUS_IN     0x02    /* controller still busy with our last write */
#define I8042_STATUS_AUX    0x20    /* the waiting byte came from port 2 */

/* Set the controller up once (both ports disabled, their IRQs off, output
 * flushed). Later calls return the first result. 0 or -ENODEV. */
int i8042_init(void);

/* Enable a port (1 or 2) with its interrupt on. 0, -ENODEV or -EIO. */
int i8042_enable_port(int port);
void i8042_disable_port(int port);

/* Send a byte to the device on `port` and wait for its ACK (0xFA). */
int i8042_device_cmd(int port, uint8_t byte);

#endif

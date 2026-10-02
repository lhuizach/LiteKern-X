/* LiteKern X — PS/2 mouse / touchpad driver (Phase 1 §6). Legacy device
 * "mouse0" on i8042 port 2, IRQ 12.
 *
 * Standard 3-byte PS/2 packets only. The EeePC's Elantech touchpad speaks
 * this until it's switched into its own absolute mode, which is a Non-Goal
 * (docs/NON-GOALS.md). Each packet becomes one struct mouse_event
 * (kernel/input.h); read() drains the queue without blocking.
 *
 *   read   struct mouse_event records; len must be a multiple of their size
 *   write  -ENOSYS
 *   ioctl  MOUSE_GET_DROPPED (uint32_t *)
 *   shutdown  masks IRQ 12 and disables the port */
#include "drivers/builtin.h"
#include "drivers/i8042.h"
#include "kernel/errno.h"
#include "kernel/input.h"
#include "kernel/io.h"
#include "kernel/irq.h"

#define MOUSE_IRQ   12
#define QUEUE_SIZE  64
#define CMD_SET_DEFAULTS    0xf6    /* 100 samples/s, 4 counts/mm, 1:1, stream mode; no reset */
#define CMD_ENABLE_REPORTING 0xf4
#define CMD_SET_RATE        0xf3    /* then the samples per second */
#define SAMPLE_RATE         200     /* PS/2's highest: the pointer moves in smaller steps */

#define PKT_SYNC     0x08   /* always set in the first byte */
#define PKT_X_SIGN   0x10
#define PKT_Y_SIGN   0x20
#define PKT_X_OVER   0x40
#define PKT_Y_OVER   0x80

static struct mouse_event queue[QUEUE_SIZE];
static volatile uint32_t head, tail;
static uint32_t dropped;
static uint8_t packet[3];
static int got;

static void push(struct mouse_event ev)
{
    if (head - tail == QUEUE_SIZE) {
        tail++;
        dropped++;
    }
    queue[head % QUEUE_SIZE] = ev;
    head++;
}

static void mouse_irq(void *ctx)
{
    (void)ctx;
    uint8_t status = inb(I8042_STATUS);
    if (!(status & I8042_STATUS_OUT) || !(status & I8042_STATUS_AUX))
        return;                     /* nothing for us (port 1 data is the keyboard's) */
    uint8_t b = inb(I8042_DATA);

    /* Resynchronise: a packet's first byte always has bit 3 set. */
    if (got == 0 && !(b & PKT_SYNC))
        return;
    packet[got++] = b;
    if (got < 3)
        return;
    got = 0;

    if (packet[0] & (PKT_X_OVER | PKT_Y_OVER))
        return;                     /* overflowed deltas are garbage */
    int dx = packet[1] - ((packet[0] & PKT_X_SIGN) ? 256 : 0);
    int dy = packet[2] - ((packet[0] & PKT_Y_SIGN) ? 256 : 0);
    push((struct mouse_event){
        .dx = (int16_t)dx,
        .dy = (int16_t)-dy,         /* PS/2 counts up as positive; screens count down */
        .buttons = packet[0] & (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE),
    });
}

static int mouse_init(device_t *dev)
{
    (void)dev;
    int err = i8042_enable_port(2);
    if (err)
        return err;
    if (i8042_device_cmd(2, CMD_SET_DEFAULTS)) {
        i8042_disable_port(2);
        return -ENODEV;             /* nothing answering on port 2 */
    }
    /* Faster reports when the device takes them (it keeps 100/s if not). */
    if (i8042_device_cmd(2, CMD_SET_RATE) || i8042_device_cmd(2, SAMPLE_RATE))
        i8042_device_cmd(2, CMD_SET_DEFAULTS);
    if (i8042_device_cmd(2, CMD_ENABLE_REPORTING)) {
        i8042_disable_port(2);
        return -ENODEV;             /* nothing answering on port 2 */
    }
    err = irq_register(MOUSE_IRQ, mouse_irq, 0);
    if (err) {
        i8042_disable_port(2);
        return err;
    }
    return 0;
}

static int mouse_read(device_t *dev, void *buf, size_t len)
{
    (void)dev;
    if (len % sizeof(struct mouse_event))
        return -EINVAL;
    struct mouse_event *out = buf;
    size_t n = 0;
    uint32_t flags = irq_save();
    while (n < len / sizeof(struct mouse_event) && tail != head) {
        out[n++] = queue[tail % QUEUE_SIZE];
        tail++;
    }
    irq_restore(flags);
    return (int)(n * sizeof(struct mouse_event));
}

static int mouse_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev;
    if (cmd != MOUSE_GET_DROPPED)
        return -ENOSYS;
    if (!arg)
        return -EINVAL;
    *(uint32_t *)arg = dropped;
    return 0;
}

static void mouse_shutdown(device_t *dev)
{
    (void)dev;
    irq_unregister(MOUSE_IRQ);
    i8042_disable_port(2);
}

const driver_t mouse_driver = {
    .name = "ps2mouse",
    .pci_ids = NULL,
    .init = mouse_init,
    .read = mouse_read,
    .write = driver_nosys_write,
    .ioctl = mouse_ioctl,
    .shutdown = mouse_shutdown,
};

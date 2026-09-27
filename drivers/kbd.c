/* LiteKern X — PS/2 keyboard driver (Phase 1 §6). Legacy device "kbd0" on
 * i8042 port 1, IRQ 1.
 *
 * The controller translates to scancode set 1. The IRQ handler decodes each
 * byte into a struct key_event (kernel/input.h) and queues it; read() drains
 * the queue without blocking. If the queue overflows, the oldest events are
 * dropped and counted (KBD_GET_DROPPED).
 *
 *   read   struct key_event records; len must be a multiple of their size
 *   write  -ENOSYS
 *   ioctl  KBD_GET_DROPPED (uint32_t *)
 *   shutdown  masks IRQ 1 and disables the port */
#include "drivers/builtin.h"
#include "drivers/i8042.h"
#include "kernel/errno.h"
#include "kernel/input.h"
#include "kernel/io.h"
#include "kernel/irq.h"

#define KBD_IRQ     1
#define QUEUE_SIZE  64          /* power of two */
#define SC_ENABLE_SCANNING 0xf4

/* US layout, scancode set 1 make codes 0x00-0x39 (+1 for the terminator). */
static const char plain[0x3b] =
    "\0\x1b" "1234567890-=\b"
    "\tqwertyuiop[]\n"
    "\0asdfghjkl;'`"
    "\0\\zxcvbnm,./\0*\0 ";
static const char shifted[0x3b] =
    "\0\x1b" "!@#$%^&*()_+\b"
    "\tQWERTYUIOP{}\n"
    "\0ASDFGHJKL:\"~"
    "\0|ZXCVBNM<>?\0*\0 ";

static struct key_event queue[QUEUE_SIZE];
static volatile uint32_t head, tail;    /* head: next write (IRQ), tail: next read */
static uint32_t dropped;
static uint8_t mods, lshift, rshift, lctrl, rctrl, lalt, ralt;
static int extended, skip;

static void push(uint16_t key, int pressed)
{
    struct key_event ev = { .key = key, .pressed = (uint8_t)pressed, .mods = mods };
    if (!(key & KEY_EXTENDED) && key < sizeof(plain)) {
        int is_letter = plain[key] >= 'a' && plain[key] <= 'z';
        int shift = (mods & MOD_SHIFT) != 0;
        if (is_letter && (mods & MOD_CAPSLOCK))
            shift = !shift;
        ev.ascii = (uint8_t)(shift ? shifted[key] : plain[key]);
        if (is_letter && (mods & MOD_CTRL))
            ev.ascii = (uint8_t)(plain[key] - 'a' + 1);
    }
    if (head - tail == QUEUE_SIZE) {    /* full: drop the oldest */
        tail++;
        dropped++;
    }
    queue[head % QUEUE_SIZE] = ev;
    head++;
}

static void update_mods(uint16_t key, int pressed)
{
    switch (key) {
    case KEY_LSHIFT: lshift = pressed; break;
    case KEY_RSHIFT: rshift = pressed; break;
    case KEY_LCTRL:  lctrl = pressed; break;
    case KEY_RCTRL:  rctrl = pressed; break;
    case KEY_LALT:   lalt = pressed; break;
    case KEY_RALT:   ralt = pressed; break;
    case KEY_CAPSLOCK:
        if (pressed)
            mods ^= MOD_CAPSLOCK;
        break;
    }
    mods = (mods & MOD_CAPSLOCK) | ((lshift || rshift) ? MOD_SHIFT : 0) |
           ((lctrl || rctrl) ? MOD_CTRL : 0) | ((lalt || ralt) ? MOD_ALT : 0);
}

static void kbd_irq(void *ctx)
{
    (void)ctx;
    uint8_t status = inb(I8042_STATUS);
    if (!(status & I8042_STATUS_OUT) || (status & I8042_STATUS_AUX))
        return;                     /* nothing for us (port 2 data is the mouse's) */
    uint8_t sc = inb(I8042_DATA);

    if (skip) {                     /* rest of the 6-byte Pause sequence */
        skip--;
        return;
    }
    if (sc == 0xe1) {
        skip = 5;
        return;
    }
    if (sc == 0xe0) {
        extended = 1;
        return;
    }
    if (sc == 0xfa || sc == 0xfe || sc == 0x00 || sc == 0xff)
        return;                     /* ACK / resend / errors, not keys */

    uint16_t key = (sc & 0x7f) | (extended ? KEY_EXTENDED : 0);
    int pressed = !(sc & 0x80);
    extended = 0;
    if (key == (KEY_EXTENDED | 0x2a) || key == (KEY_EXTENDED | 0x36))
        return;                     /* fake shifts around PrtSc etc. */
    update_mods(key, pressed);
    push(key, pressed);
}

static int kbd_init(device_t *dev)
{
    (void)dev;
    int err = i8042_enable_port(1);
    if (err)
        return err;
    /* The BIOS normally leaves scanning on; make sure, without a slow reset. */
    if (i8042_device_cmd(1, SC_ENABLE_SCANNING)) {
        i8042_disable_port(1);
        return -ENODEV;             /* no keyboard answering on port 1 */
    }
    err = irq_register(KBD_IRQ, kbd_irq, 0);
    if (err) {
        i8042_disable_port(1);
        return err;
    }
    return 0;
}

static int kbd_read(device_t *dev, void *buf, size_t len)
{
    (void)dev;
    if (len % sizeof(struct key_event))
        return -EINVAL;
    struct key_event *out = buf;
    size_t n = 0;
    uint32_t flags = irq_save();            /* the IRQ handler moves tail on overflow */
    while (n < len / sizeof(struct key_event) && tail != head) {
        out[n++] = queue[tail % QUEUE_SIZE];
        tail++;
    }
    irq_restore(flags);
    return (int)(n * sizeof(struct key_event));
}

static int kbd_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev;
    if (cmd != KBD_GET_DROPPED)
        return -ENOSYS;
    if (!arg)
        return -EINVAL;
    *(uint32_t *)arg = dropped;
    return 0;
}

static void kbd_shutdown(device_t *dev)
{
    (void)dev;
    irq_unregister(KBD_IRQ);
    i8042_disable_port(1);
}

const driver_t kbd_driver = {
    .name = "ps2kbd",
    .pci_ids = NULL,
    .init = kbd_init,
    .read = kbd_read,
    .write = driver_nosys_write,
    .ioctl = kbd_ioctl,
    .shutdown = kbd_shutdown,
};

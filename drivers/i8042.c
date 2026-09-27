#include "drivers/i8042.h"
#include "kernel/errno.h"
#include "kernel/io.h"

#define CMD_READ_CONFIG     0x20
#define CMD_WRITE_CONFIG    0x60
#define CMD_DISABLE_PORT2   0xa7
#define CMD_ENABLE_PORT2    0xa8
#define CMD_TEST_PORT2      0xa9
#define CMD_TEST_PORT1      0xab
#define CMD_DISABLE_PORT1   0xad
#define CMD_ENABLE_PORT1    0xae
#define CMD_WRITE_PORT2     0xd4

#define CFG_IRQ1            0x01
#define CFG_IRQ12           0x02
#define CFG_PORT2_CLOCK_OFF 0x20

#define ACK                 0xfa
#define SPINS               100000

static int state;           /* 0 = not yet, 1 = ok, <0 = error */
static int has_port2;

static int wait_write(void)
{
    for (int i = 0; i < SPINS; i++)
        if (!(inb(I8042_STATUS) & I8042_STATUS_IN))
            return 0;
    return -EIO;
}

static int wait_read(void)
{
    for (int i = 0; i < SPINS; i++)
        if (inb(I8042_STATUS) & I8042_STATUS_OUT)
            return 0;
    return -EIO;
}

static int command(uint8_t cmd)
{
    if (wait_write())
        return -EIO;
    outb(I8042_STATUS, cmd);
    return 0;
}

static int read_data(void)
{
    if (wait_read())
        return -EIO;
    return inb(I8042_DATA);
}

static int write_data(uint8_t v)
{
    if (wait_write())
        return -EIO;
    outb(I8042_DATA, v);
    return 0;
}

static int read_config(void)
{
    return command(CMD_READ_CONFIG) ? -EIO : read_data();
}

static int write_config(uint8_t cfg)
{
    return (command(CMD_WRITE_CONFIG) || write_data(cfg)) ? -EIO : 0;
}

static void flush(void)
{
    for (int i = 0; i < 64 && (inb(I8042_STATUS) & I8042_STATUS_OUT); i++)
        inb(I8042_DATA);
}

static int setup(void)
{
    /* Nothing decodes port 0x64 when there's no controller: reads 0xff. */
    if (inb(I8042_STATUS) == 0xff)
        return -ENODEV;
    if (command(CMD_DISABLE_PORT1) || command(CMD_DISABLE_PORT2))
        return -ENODEV;
    flush();

    int cfg = read_config();
    if (cfg < 0)
        return -ENODEV;
    cfg &= ~(CFG_IRQ1 | CFG_IRQ12);     /* IRQs back on per port, when enabled */
    if (write_config((uint8_t)cfg))
        return -ENODEV;

    /* Dual-channel controller: enabling port 2 clears its "clock off" bit. */
    if (!command(CMD_ENABLE_PORT2)) {
        int c = read_config();
        has_port2 = c >= 0 && !(c & CFG_PORT2_CLOCK_OFF);
        command(CMD_DISABLE_PORT2);
    }
    flush();
    return 0;
}

int i8042_init(void)
{
    if (!state)
        state = setup() ? -ENODEV : 1;
    return state < 0 ? state : 0;
}

int i8042_enable_port(int port)
{
    if (i8042_init() || (port != 1 && port != 2) || (port == 2 && !has_port2))
        return -ENODEV;
    if (command(port == 1 ? CMD_TEST_PORT1 : CMD_TEST_PORT2) || read_data() != 0x00)
        return -ENODEV;     /* the port's interface test failed: nothing usable there */
    int cfg = read_config();
    if (cfg < 0)
        return -EIO;
    cfg |= port == 1 ? CFG_IRQ1 : CFG_IRQ12;
    if (write_config((uint8_t)cfg) || command(port == 1 ? CMD_ENABLE_PORT1 : CMD_ENABLE_PORT2))
        return -EIO;
    return 0;
}

void i8042_disable_port(int port)
{
    command(port == 1 ? CMD_DISABLE_PORT1 : CMD_DISABLE_PORT2);
    int cfg = read_config();
    if (cfg >= 0)
        write_config((uint8_t)(cfg & ~(port == 1 ? CFG_IRQ1 : CFG_IRQ12)));
}

int i8042_device_cmd(int port, uint8_t byte)
{
    if (port == 2 && command(CMD_WRITE_PORT2))
        return -EIO;
    if (write_data(byte))
        return -EIO;
    /* Interrupts for this port may already be on, so poll for the ACK here
     * with the caller holding interrupts off. Only a byte from the same port
     * counts: with the keyboard live, a key press can arrive in between. */
    for (int tries = 0; tries < 8; tries++) {
        if (wait_read())
            return -EIO;
        int from_port2 = (inb(I8042_STATUS) & I8042_STATUS_AUX) != 0;
        uint8_t r = inb(I8042_DATA);
        if (from_port2 != (port == 2))
            continue;               /* the other device's byte: not our answer */
        if (r == ACK)
            return 0;
    }
    return -EIO;
}

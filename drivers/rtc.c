/* LiteKern X — CMOS real-time clock driver. Legacy device "rtc0" (ports
 * 0x70/0x71, IRQ 8). Interface: kernel/rtc.h.
 *
 * The time is read once at init (waiting out an update in progress) and
 * then again from the "update ended" interrupt, which the chip raises right
 * after it has updated its registers once a second: registers are always
 * valid then, and the interrupt doubles as a 1 Hz tick. On request the chip
 * also raises its periodic interrupt (RTC_SET_RATE), which only wakes the
 * CPU: the handler acknowledges it and returns. */
#include "drivers/builtin.h"
#include "kernel/errno.h"
#include "kernel/io.h"
#include "kernel/irq.h"
#include "kernel/rtc.h"

#define RTC_INDEX   0x70
#define RTC_DATA    0x71
#define RTC_IRQ     8

#define REG_SEC     0x00
#define REG_MIN     0x02
#define REG_HOUR    0x04
#define REG_DAY     0x07
#define REG_MONTH   0x08
#define REG_YEAR    0x09
#define REG_A       0x0a
#define REG_B       0x0b
#define REG_C       0x0c

#define A_UIP       0x80    /* update in progress */
#define B_24H       0x02
#define B_BINARY    0x04
#define A_RATE      0x0f    /* periodic rate: 32768 >> (rate - 1) Hz */
#define B_PIE       0x40    /* periodic interrupt enable */
#define B_UIE       0x10    /* update-ended interrupt enable */
#define C_UF        0x10    /* an update ended */
#define HOUR_PM     0x80    /* 12-hour mode */

static struct rtc_time now;
static volatile uint32_t ticks;

static uint8_t reg(uint8_t r)
{
    outb(RTC_INDEX, r);
    return inb(RTC_DATA);
}

static void set_reg(uint8_t r, uint8_t v)
{
    outb(RTC_INDEX, r);
    outb(RTC_DATA, v);
}

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0f));
}

/* Sakamoto's method: 0 = Sunday. */
static uint8_t weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y--;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

struct rtc_time rtc_decode(struct rtc_raw raw)
{
    int binary = raw.status_b & B_BINARY, pm = 0;
    uint8_t hour = raw.hour;
    if (!(raw.status_b & B_24H)) {
        pm = hour & HOUR_PM;
        hour &= (uint8_t)~HOUR_PM;
    }
    struct rtc_time t = {
        .second = binary ? raw.sec : bcd(raw.sec),
        .minute = binary ? raw.min : bcd(raw.min),
        .hour = binary ? hour : bcd(hour),
        .day = binary ? raw.day : bcd(raw.day),
        .month = binary ? raw.month : bcd(raw.month),
        .year = (uint16_t)(2000 + (binary ? raw.year : bcd(raw.year))),
    };
    if (!(raw.status_b & B_24H))    /* 12 AM is 0:xx, 12 PM stays 12:xx */
        t.hour = (uint8_t)(t.hour % 12 + (pm ? 12 : 0));
    if (t.month >= 1 && t.month <= 12)
        t.weekday = weekday(t.year, t.month, t.day);
    return t;
}

static struct rtc_time read_now(void)
{
    return rtc_decode((struct rtc_raw){ reg(REG_SEC), reg(REG_MIN), reg(REG_HOUR), reg(REG_DAY),
                                        reg(REG_MONTH), reg(REG_YEAR), reg(REG_B) });
}

static void rtc_irq(void *ctx)
{
    (void)ctx;
    if (!(reg(REG_C) & C_UF))       /* reading C also acknowledges the interrupt */
        return;
    now = read_now();
    ticks++;
}

static int rtc_init(device_t *dev)
{
    (void)dev;
    /* An absent chip reads as 0xff everywhere. */
    if (reg(REG_B) == 0xff && reg(REG_A) == 0xff)
        return -ENODEV;
    for (int i = 0; i < 100000 && (reg(REG_A) & A_UIP); i++)
        ;                           /* at most ~2 ms, once */
    now = read_now();
    if (!now.month || now.month > 12 || !now.day || now.day > 31)
        return -EIO;
    int err = irq_register(RTC_IRQ, rtc_irq, 0);
    if (err)
        return err;
    uint32_t flags = irq_save();
    set_reg(REG_B, (uint8_t)(reg(REG_B) | B_UIE));
    reg(REG_C);                     /* clear anything pending, or IRQ 8 never fires */
    irq_restore(flags);
    return 0;
}

static int rtc_read(device_t *dev, void *buf, size_t len)
{
    (void)dev;
    if (len < sizeof(struct rtc_time))
        return -EINVAL;
    uint32_t flags = irq_save();
    *(struct rtc_time *)buf = now;
    irq_restore(flags);
    return (int)sizeof(struct rtc_time);
}

static int set_rate(uint32_t hz)
{
    int rate = 0;
    for (int r = 3; r <= 15; r++)
        if (hz == (32768u >> (r - 1)))
            rate = r;
    if (hz && !rate)
        return -EINVAL;
    uint32_t flags = irq_save();
    if (rate) {
        set_reg(REG_A, (uint8_t)((reg(REG_A) & ~A_RATE) | rate));
        set_reg(REG_B, (uint8_t)(reg(REG_B) | B_PIE));
    } else {
        set_reg(REG_B, (uint8_t)(reg(REG_B) & ~B_PIE));
    }
    reg(REG_C);
    irq_restore(flags);
    return 0;
}

static int rtc_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev;
    if (!arg)
        return cmd == RTC_GET_TICKS || cmd == RTC_SET_RATE ? -EINVAL : -ENOSYS;
    switch (cmd) {
    case RTC_GET_TICKS:
        *(uint32_t *)arg = ticks;
        return 0;
    case RTC_SET_RATE:
        return set_rate(*(const uint32_t *)arg);
    default:
        return -ENOSYS;
    }
}

static void rtc_shutdown(device_t *dev)
{
    (void)dev;
    set_reg(REG_B, (uint8_t)(reg(REG_B) & ~(B_UIE | B_PIE)));
    irq_unregister(RTC_IRQ);
}

const driver_t rtc_driver = {
    .name = "cmos-rtc",
    .pci_ids = NULL,
    .init = rtc_init,
    .read = rtc_read,
    .write = driver_nosys_write,
    .ioctl = rtc_ioctl,
    .shutdown = rtc_shutdown,
};

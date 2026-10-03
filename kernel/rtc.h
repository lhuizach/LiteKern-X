/* LiteKern X — the real-time clock's interface (drivers/rtc.c, device "rtc0").
 *
 * The time is whatever the PC's clock holds: local time on a machine set up
 * by Windows, like the EeePC (the VMs are started the same way). No time
 * zones (docs/NON-GOALS.md: localisation is Phase 5).
 *
 *   read   one struct rtc_time: the time as of the last second's tick
 *   ioctl  RTC_GET_TICKS (uint32_t *): seconds counted since init, one per
 *          IRQ 8 "update ended" interrupt, so the shell can wake once a
 *          second without a timer
 *   ioctl  RTC_SET_RATE (const uint32_t *hz): also raise IRQ 8 hz times a
 *          second (a power of two, 2..8192), or stop with 0. The kernel has
 *          no other timer: this is what wakes a `hlt` for each frame of an
 *          animation (the screen saver) instead of spinning. -EINVAL for
 *          other rates */
#ifndef LKX_RTC_H
#define LKX_RTC_H

#include <stdint.h>

#define RTC_GET_TICKS 1
#define RTC_SET_RATE  2

struct rtc_time {
    uint16_t year;          /* e.g. 2026 */
    uint8_t month, day;     /* 1-12, 1-31 */
    uint8_t hour, minute, second;
    uint8_t weekday;        /* 0 = Sunday, computed from the date */
};

/* The chip's registers as read -> struct rtc_time, handling BCD and 12-hour
 * modes (status register B). Exposed for the self-test. */
struct rtc_raw {
    uint8_t sec, min, hour, day, month, year, status_b;
};
struct rtc_time rtc_decode(struct rtc_raw raw);

#endif

#include <stdint.h>
#include "kernel/console.h"
#include "kernel/printk.h"
#include "kernel/serial.h"
#include "kernel/status.h"
#include "kernel/io.h"

/* Everything logged since boot, so the on-screen console can replay it once
 * the display driver is up. Once full, later output still goes to serial and
 * the console, just not into the buffer. */
static char log_buf[16 * 1024];
static uint32_t log_len;
static int panicking;

static void out(char c)
{
    serial_putc(c);
    if (log_len < sizeof(log_buf))
        log_buf[log_len++] = c;
    if (panicking < 2)
        console_putc(c);
}

const char *printk_log(uint32_t *len)
{
    *len = log_len;
    return log_buf;
}

static void put_str(const char *s)
{
    while (*s)
        out(*s++);
}

static void put_uint(uint64_t v, unsigned base, int width, char pad)
{
    char buf[20];
    int n = 0;
    do {
        buf[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (width-- > n)
        out(pad);
    while (n)
        out(buf[--n]);
}

void vkprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt);
            continue;
        }
        fmt++;
        char pad = ' ';
        int width = 0;
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int is_64 = 0;
        if (fmt[0] == 'l' && fmt[1] == 'l') {
            is_64 = 1;
            fmt += 2;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            put_str(s ? s : "(null)");
            break;
        }
        case 'c':
            out((char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                out('-');
                put_uint(-(uint32_t)v, 10, width ? width - 1 : 0, pad);
            } else {
                put_uint((uint32_t)v, 10, width, pad);
            }
            break;
        }
        case 'u':
        case 'x': {
            uint64_t v = is_64 ? va_arg(ap, uint64_t) : va_arg(ap, uint32_t);
            put_uint(v, *fmt == 'x' ? 16 : 10, width, pad);
            break;
        }
        case 'p':
            put_str("0x");
            put_uint((uint32_t)va_arg(ap, void *), 16, 8, '0');
            break;
        case '%':
            out('%');
            break;
        case '\0':
            return;
        default:                    /* unknown: print it verbatim */
            out('%');
            out(*fmt);
            break;
        }
    }
}

void kwrite(const char *s, unsigned n)
{
    while (n--)
        out(*s++);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...)
{
    va_list ap;
    __asm__ volatile("cli");
    /* A panic while panicking (e.g. a fault while drawing the first panic)
     * stays on serial only and skips the screen. */
    if (++panicking > 1) {
        put_str("\nPANIC (nested): ");
        va_start(ap, fmt);
        vkprintf(fmt, ap);
        va_end(ap);
        put_str("\n");
        halt_forever();
    }
    put_str("\nPANIC: ");
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
    put_str("\n");
    status_show(STATUS_PANIC);
    halt_forever();
}

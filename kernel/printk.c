#include <stdint.h>
#include "kernel/printk.h"
#include "kernel/serial.h"
#include "kernel/status.h"
#include "kernel/io.h"

static void put_str(const char *s)
{
    while (*s)
        serial_putc(*s++);
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
        serial_putc(pad);
    while (n)
        serial_putc(buf[--n]);
}

void vkprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            serial_putc(*fmt);
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
            serial_putc((char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                serial_putc('-');
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
            serial_putc('%');
            break;
        case '\0':
            return;
        default:                    /* unknown: print it verbatim */
            serial_putc('%');
            serial_putc(*fmt);
            break;
        }
    }
}

void kwrite(const char *s, unsigned n)
{
    while (n--)
        serial_putc(*s++);
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
    put_str("\nPANIC: ");
    va_start(ap, fmt);
    vkprintf(fmt, ap);
    va_end(ap);
    put_str("\n");
    status_show(STATUS_PANIC);
    halt_forever();
}

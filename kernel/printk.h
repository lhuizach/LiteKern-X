/* LiteKern X — kernel logging.
 * Formats: %s %c %d %u %x %p %%, with optional '0' flag and width (e.g. %08x). */
#ifndef LKX_PRINTK_H
#define LKX_PRINTK_H

#include <stdarg.h>

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vkprintf(const char *fmt, va_list ap);

/* Print "PANIC: ...", mark the screen as failed, halt. */
void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

#endif

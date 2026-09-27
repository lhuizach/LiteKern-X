/* LiteKern X — kernel logging.
 * Formats: %s %c %d %u %x %p %%, with optional '0' flag and width (e.g. %08x),
 * and 'll' for 64-bit %llu / %llx. */
#ifndef LKX_PRINTK_H
#define LKX_PRINTK_H

#include <stdarg.h>
#include <stdint.h>

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vkprintf(const char *fmt, va_list ap);

/* Write exactly n bytes (no formatting, no NUL needed). */
void kwrite(const char *s, unsigned n);

/* Everything logged since boot (up to a fixed size), for the console. */
const char *printk_log(uint32_t *len);

/* Print "PANIC: ...", mark the screen as failed, halt. */
void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

#endif

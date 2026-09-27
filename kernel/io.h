/* LiteKern X — port I/O and small CPU helpers. */
#ifndef LKX_IO_H
#define LKX_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outl(uint16_t port, uint32_t v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint32_t read_cr2(void)
{
    uint32_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

/* Disable interrupts, returning the previous EFLAGS for irq_restore(). */
static inline uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

static inline __attribute__((noreturn)) void halt_forever(void)
{
    for (;;)
        __asm__ volatile("cli; hlt");
}

#endif

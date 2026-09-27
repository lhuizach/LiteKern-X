/* LiteKern X — interrupt descriptor table. Only CPU exceptions (0-31) are
 * installed; IRQs arrive with the first interrupt-driven driver. */
#ifndef LKX_IDT_H
#define LKX_IDT_H

#include <stdint.h>

/* Stack layout built by kernel/isr.asm (pushad order, then vector/error,
 * then what the CPU pushed). */
struct int_frame {
    uint32_t edi, esi, ebp, esp_at_pushad, ebx, edx, ecx, eax;
    uint32_t vector, error;
    uint32_t eip, cs, eflags;
};

void idt_init(void);

#endif

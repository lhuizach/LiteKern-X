/* LiteKern X — interrupt descriptor table: CPU exceptions (0-31), hardware
 * IRQs (32-47, see kernel/irq.h) and the system call gate (0x80, callable
 * from ring 3). */
#ifndef LKX_IDT_H
#define LKX_IDT_H

#include <stdint.h>

#define SYSCALL_VECTOR 0x80

/* Stack layout built by kernel/isr.asm: saved segments, pushad, vector and
 * error code, then what the CPU pushed. user_esp/user_ss exist only for
 * traps from ring 3. */
struct int_frame {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_at_pushad, ebx, edx, ecx, eax;
    uint32_t vector, error;
    uint32_t eip, cs, eflags;
    uint32_t user_esp, user_ss;
};

void idt_init(void);

#endif

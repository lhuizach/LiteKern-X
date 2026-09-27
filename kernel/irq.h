/* LiteKern X — hardware interrupts through the legacy 8259 PICs (Phase 1 §6).
 *
 * IRQ 0-15 are remapped to vectors 32-47 and all start masked; a line is
 * unmasked only when a driver registers a handler for it. Handlers run with
 * interrupts disabled and must be short (read the device, queue, return). */
#ifndef LKX_IRQ_H
#define LKX_IRQ_H

#include <stdint.h>

#define IRQ_BASE_VECTOR 32
#define IRQ_COUNT       16

typedef void (*irq_handler_t)(void *ctx);

/* Remap and mask both PICs. Interrupts stay disabled (no `sti`). */
void irq_init(void);

/* Install a handler and unmask the line. Returns 0, -EINVAL (bad IRQ) or
 * -EBUSY (line already taken). */
int irq_register(unsigned irq, irq_handler_t handler, void *ctx);

/* Mask the line and remove its handler. */
void irq_unregister(unsigned irq);

/* Called from interrupt_handler() for vectors 32-47. */
void irq_dispatch(unsigned irq);

/* Interrupts seen per line (spurious ones not counted), for diagnostics. */
uint32_t irq_count(unsigned irq);

#endif

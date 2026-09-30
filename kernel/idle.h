/* LiteKern X — the input loop (kernel/main.c).
 *
 * Between apps the kernel runs it forever; while a ring 3 app waits for its
 * next event (SYS_WAIT_EVENT), the call runs it, so the pointer, the clock
 * and the top bar keep working. */
#ifndef LKX_IDLE_H
#define LKX_IDLE_H

/* Handle the input waiting (keys, pointer), or sleep until the next
 * interrupt if there is none. Call with interrupts enabled. */
void idle_step(void);

#endif

/* LiteKern X — the input loop (kernel/main.c).
 *
 * The kernel runs it forever, between giving the apps their turns
 * (kernel/lkx.h): apps waiting for an event (SYS_WAIT_EVENT) are asleep,
 * and the input that wakes them comes through here. */
#ifndef LKX_IDLE_H
#define LKX_IDLE_H

/* Handle the input waiting (keys, pointer), or sleep until the next
 * interrupt if there is none (unless `busy`: an app just ran, so there may
 * be more to do). Call with interrupts enabled. */
void idle_step(int busy);

#endif

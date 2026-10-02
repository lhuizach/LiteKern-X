/* LiteKern X — running code in ring 3 (Phase 1 §5; several at once since
 * Phase 3 §6).
 *
 * A user program is a struct user_proc: its own address space (kernel/vmm.h)
 * with a flat image at USER_BASE and a stack below USER_STACK_TOP, and the
 * registers it continues from. user_resume() runs one until it either ends
 * (exit, a fault, the watchdog) or yields: a system call that has to wait
 * (SYS_WAIT_EVENT) saves its registers with user_yield() and gives the CPU
 * back. The kernel later resumes it as if the call had just returned.
 * Nothing in the kernel ever waits inside a program: one trap stack serves
 * them all. A fault in ring 3 never takes the kernel down: only that program
 * ends. */
#ifndef LKX_USER_H
#define LKX_USER_H

#include <stdint.h>
#include "kernel/idt.h"

#define USER_BASE        0x80000000u
#define USER_TOP         0xc0000000u
#define USER_STACK_TOP   (USER_TOP - 0x1000u)   /* one unmapped guard page above */
#define USER_STACK_PAGES 4u
#define USER_IMAGE_MAX   (1024u * 1024u)

struct user_result {
    int killed;         /* 1 if a fault (or the watchdog) ended the program */
    int hung;           /* 1 if it was the watchdog: no call for 10 s */
    uint32_t vector;    /* the exception that killed it */
    int exit_code;      /* from the exit syscall, when not killed */
};

struct user_proc {
    uint32_t pd;                /* its address space */
    struct int_frame frame;     /* where it continues */
    uint32_t last_call_ms;      /* the watchdog */
    struct user_result res;     /* how it ended */
};

/* Make a program: `bss` zero-filled bytes after the image, starting at
 * `entry` (inside the image). 0, -EINVAL or -ENOMEM. */
int user_create(struct user_proc *p, const void *image, uint32_t size, uint32_t bss, uint32_t entry);

/* Run it until it yields (returns 1) or ends (returns 0: see p->res).
 * Interrupts stay on in ring 3 and come back off. */
int user_resume(struct user_proc *p);

/* Free everything it had (once it has ended, or to stop it). */
void user_destroy(struct user_proc *p);

/* The whole life of a simple program: create, run to the end, destroy.
 * Returns 0 once it has finished (see *res), or -EINVAL / -ENOMEM. */
int user_exec(const void *image, uint32_t size, struct user_result *res);

/* The program in ring 3 or in a system call right now, or NULL. */
struct user_proc *user_current(void);

/* Map fresh zeroed pages for the running program at [virt, virt + bytes).
 * Pages already mapped are kept. */
int user_map(uint32_t virt, uint32_t bytes);
/* Map `bytes` of kernel memory at `phys` (page-aligned, contiguous) into the
 * running program at `virt`, writable and shared: it stays the kernel's
 * (a window's pixels). */
int user_map_shared(uint32_t virt, uint32_t phys, uint32_t bytes);

/* From a system call: save the caller's registers (f) and give the CPU
 * back; user_resume() returns 1. Resuming it later returns from the call
 * with whatever is in p->frame.eax. */
void user_yield(const struct int_frame *f) __attribute__((noreturn));

/* Every syscall calls user_alive(); every IRQ that interrupts ring 3 calls
 * user_watchdog(), which stops a program that hasn't made a call for 10 s
 * (stuck in a loop: nothing else would ever get the machine back). */
void user_alive(void);
void user_watchdog(const struct int_frame *f);

/* 0 if [ptr, ptr+len) lies in user space and every page is mapped for ring 3
 * (and writable, if `write`); -EFAULT otherwise. Check before touching any
 * pointer that came from user space. */
int user_check(uint32_t ptr, uint32_t len, int write);

/* Called on a ring 3 fault after it has been reported: kill the program. */
void user_kill(const struct int_frame *f) __attribute__((noreturn));

/* The exit syscall. */
void user_exit(int code) __attribute__((noreturn));

#endif

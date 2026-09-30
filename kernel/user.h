/* LiteKern X — running code in ring 3 (Phase 1 §5).
 *
 * One user program at a time. user_exec() maps a flat binary at USER_BASE
 * plus a stack below USER_STACK_TOP, runs it in ring 3 until it exits or is
 * killed by a fault, then unmaps and frees everything. A fault in ring 3
 * never takes the kernel down: the program is killed and user_exec returns. */
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

/* Load and run `image` (entry point = USER_BASE). Returns 0 once the program
 * has finished (see *res), or -EINVAL / -ENOMEM if it couldn't be started. */
int user_exec(const void *image, uint32_t size, struct user_result *res);

/* The same, for an app: `bss` zero-filled bytes are mapped after the image,
 * and it starts at `entry` (inside the image). Interrupts stay on while it
 * runs, so input keeps arriving. */
int user_exec_app(const void *image, uint32_t size, uint32_t bss, uint32_t entry,
                  struct user_result *res);

/* Map fresh zeroed pages for the running program at [virt, virt + bytes)
 * (e.g. an app's canvas). Pages already mapped are kept. */
int user_map(uint32_t virt, uint32_t bytes);

/* 1 while a user program runs (the kernel is then inside user_exec). */
int user_running(void);

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

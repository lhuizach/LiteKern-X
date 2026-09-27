/* LiteKern X — system calls (Phase 1 §5).
 *
 * ABI: `int 0x80` from ring 3. EAX = syscall number, EBX / ECX / EDX = args,
 * result in EAX (negative errno on failure). Every other register is
 * preserved. Pointers from user space are checked with user_check() before
 * the kernel touches them.
 *
 *   0  exit(int code)                    does not return
 *   1  debug_write(const char *buf, n)   log "user: <buf>"; n <= 4096;
 *                                        returns n, -EFAULT, -EINVAL
 *   2  uptime_ms()                       ms since stage 1 started (T0)
 *   *  anything else                     -ENOSYS
 *
 * The syscall table grows only when an app needs it (Phase 2 §5). */
#ifndef LKX_SYSCALL_H
#define LKX_SYSCALL_H

#include "kernel/idt.h"

#define SYS_EXIT        0
#define SYS_DEBUG_WRITE 1
#define SYS_UPTIME_MS   2

#define DEBUG_WRITE_MAX 4096u

void syscall_dispatch(struct int_frame *f);

#endif

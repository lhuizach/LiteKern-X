/* LiteKern X — the app system calls (Phase 2 §5): window, events, font and
 * files, for KERN86 apps in ring 3. Numbers and structures:
 * kernel/kern86_abi.h. */
#ifndef LKX_SYS_APP_H
#define LKX_SYS_APP_H

#include <stdint.h>

/* The calls above SYS_UPTIME_MS; -ENOSYS for unknown numbers. */
int sys_app(uint32_t nr, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

/* Around each app run (kernel/lkx.c). */
void sys_app_start(const char *name);
void sys_app_end(void);

#endif

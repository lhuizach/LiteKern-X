/* LiteKern X — the app system calls (Phase 2 §5): window, events, font and
 * files, for KERN86 apps in ring 3. Numbers and structures:
 * kernel/kern86_abi.h. */
#ifndef LKX_SYS_APP_H
#define LKX_SYS_APP_H

#include <stdint.h>

/* The calls above SYS_UPTIME_MS (but SYS_WAIT_EVENT: kernel/lkx.h); -ENOSYS
 * for unknown numbers. */
int sys_app(uint32_t nr, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

#endif

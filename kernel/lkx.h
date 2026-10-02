/* LiteKern X — KERN86 apps (Phase 2 §5): ported from v1's kern86/loader.c.
 *
 * An app is a folder in the ramdisk, apps/<name>/, with
 *   kerns.json   {"name": "Files", "icon": "files", "entry": "files.lkx"}
 *   <entry>      the program: a struct lkx_header (kernel/kern86_abi.h),
 *                then its image, loaded at K86_APP_BASE
 * and it runs in ring 3, talking to the kernel through int 0x80 (the SDK's
 * sdk/kern86.h). v1 read "KERNS.JSN" and "APP.BIN" from the FAT disk and
 * called into the kernel directly; here the ramdisk holds them and a crash
 * ends only the app. */
#ifndef LKX_LKX_H
#define LKX_LKX_H

#include "kernel/apps.h"
#include "kernel/idt.h"
#include "kernel/wm.h"

/* Add every app found in the ramdisk to the app list (kernel/apps.h). */
void lkx_register_all(void);

/* Start the app in its window (already open). It runs whenever it has
 * something to do (lkx_schedule); when it ends, how is logged and the shell
 * is told (desktop_app_ended). 0 or a negative errno. */
int lkx_start(const struct app *a, struct wm_window *w);

/* Give every app that has something to do a turn: one just started, or one
 * waiting in SYS_WAIT_EVENT whose window has an event. Each runs until it
 * waits again (or ends). Returns how many ran. */
int lkx_schedule(void);

/* The running app's window (inside its system calls), or NULL. */
struct wm_window *lkx_window(void);
/* SYS_WAIT_EVENT: the next event now, or sleep until there is one. */
void lkx_wait_event(struct int_frame *f);
/* Is the app running? */
int lkx_running(const struct app *a);

#endif

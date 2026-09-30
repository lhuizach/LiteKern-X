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

/* Add every app found in the ramdisk to the app list (kernel/apps.h). */
void lkx_register_all(void);

/* Load and run the app (its window is already open) until it exits or is
 * stopped. Logs how it ended. */
void lkx_run(const struct app *a);

#endif

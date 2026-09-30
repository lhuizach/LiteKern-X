/* LiteKern X — the apps on the desktop (Phase 2).
 *
 * KERN86 apps from the ramdisk (kernel/lkx.h: ring 3 programs, described by
 * their kerns.json), then Log, which stays in the kernel: it shows the
 * kernel's own log. */
#ifndef LKX_APPS_H
#define LKX_APPS_H

#include "kernel/input.h"
#include "kernel/wm.h"

struct app {
    const char *name;       /* under its icon, and the window title */
    const char *icon;       /* an assets/icons.json name */
    void (*open)(void);     /* after wm_open(): draw the content */
    void (*event)(const struct wm_event *ev);   /* NULL: events are only logged */
    void (*close)(void);    /* before wm_close(); may be NULL */
    const char *lkx;        /* a ring 3 app: its program in the ramdisk (then the
                             * hooks above are unused) */
};

#define APPS_MAX 16
extern struct app builtin_apps[APPS_MAX];
extern int builtin_app_count;

/* Build the list: the ramdisk's apps, then Log. Once, at boot. */
void apps_init(void);
/* Add one (kernel/lkx.c). 0, or -ENOMEM when the list is full. */
int apps_add(const struct app *a);

/* Log a key press ("kbd: key ..."). PgUp/PgDn/Home/End scroll the log
 * instead (on the EeePC: Fn + arrows). */
void log_key(const struct key_event *ev);

#endif

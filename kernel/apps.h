/* LiteKern X — the apps on the desktop (Phase 2).
 *
 * Built into the kernel for now: Files (a demo on an in-memory folder until
 * storage, §5a) and Log (the boot log in a window). Phase 2 §5 replaces this list with
 * KERN86 apps loaded from the ramdisk, described by kerns.json. */
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
};

extern const struct app builtin_apps[];
extern const int builtin_app_count;

/* Files (kernel/files_app.c): a demo on an in-memory folder until storage. */
void files_open(void);
void files_event(const struct wm_event *ev);

/* Log a key press ("kbd: key ..."). PgUp/PgDn/Home/End scroll the log
 * instead (on the EeePC: Fn + arrows). */
void log_key(const struct key_event *ev);

#endif

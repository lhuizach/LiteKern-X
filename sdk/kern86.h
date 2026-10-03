/* LiteKern X — kern86.h, the app side of KERN86 (Phase 2 §5).
 *
 * Include this in an app (apps/<name>/). Every call is a thin wrapper over
 * the kernel's `int 0x80` gate (numbers and structures: kernel/kern86_abi.h),
 * where v1's apps called kernel functions directly. Apps also get the
 * kernel's drawing and widget code (kernel/gfx.h, kernel/widget.h), built
 * into them: they draw into their canvas and show it with k86_present().
 *
 * An app's life: k86_window_open(), draw, k86_present(), then loop on
 * k86_wait_event() until K86_EVENT_CLOSE (returning from main, or
 * k86_exit(), ends it; so does the next k86_wait_event() after a close). */
#ifndef LKX_SDK_KERN86_H
#define LKX_SDK_KERN86_H

#include <stdint.h>
#include "kernel/kern86_abi.h"

static inline int k86_call(uint32_t nr, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory");
    return ret;
}

static inline int k86_call5(uint32_t nr, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return ret;
}

static inline __attribute__((noreturn)) void k86_exit(int code)
{
    k86_call(SYS_EXIT, (uint32_t)code, 0, 0, 0);
    __builtin_unreachable();
}

/* Log a line ("user: <text>", in the Log app and on serial). */
int k86_log(const char *text);
/* printf-lite into the log: %s %d %u %x only. */
void k86_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static inline uint32_t k86_uptime_ms(void)
{
    return (uint32_t)k86_call(SYS_UPTIME_MS, 0, 0, 0, 0);
}

/* The window: its size and canvas. Also loads the font for kernel/gfx.h. */
int k86_window_open(struct k86_window *w);

static inline int k86_present(int x, int y, int w, int h)
{
    return k86_call(SYS_WINDOW_PRESENT, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

static inline int k86_header(const struct k86_header *h)
{
    return k86_call(SYS_WINDOW_HEADER, (uint32_t)h, 0, 0, 0);
}

/* Sleeps until an event comes. On K86_EVENT_THEME the app's copy of the
 * theme (kernel/theme.h's theme_get()) is already updated: just redraw. */
int k86_wait_event(struct k86_event *ev);

/* Appearance (the Settings app). */
static inline int k86_appearance(struct k86_appearance *out)
{
    return k86_call(SYS_APPEARANCE, (uint32_t)out, 0, 0, 0);
}

static inline int k86_appearance_set(int style, int accent, const char *wallpaper)
{
    return k86_call(SYS_APPEARANCE_SET, (uint32_t)style, (uint32_t)accent, (uint32_t)wallpaper, 0);
}

/* Seconds without input before the screen saver starts; 0 turns it off. */
static inline int k86_screensaver_set(int seconds)
{
    return k86_call(SYS_SCREENSAVER_SET, (uint32_t)seconds, 0, 0, 0);
}

/* Show the screen saver now (any input ends it). */
static inline int k86_screensaver_preview(void)
{
    return k86_call(SYS_SCREENSAVER_SET, 0, 1, 0, 0);
}

static inline int k86_wallpaper_thumb(const char *name, uint32_t *out, int w, int h)
{
    return k86_call(SYS_WALLPAPER_THUMB, (uint32_t)name, (uint32_t)out, (uint32_t)w, (uint32_t)h);
}

/* Files. Paths start with '/', inside a volume (index from k86_volumes). */
static inline int k86_volumes(struct k86_volume *out, int max)
{
    return k86_call(SYS_FS_VOLUMES, (uint32_t)out, (uint32_t)max, 0, 0);
}

static inline int k86_list(int vol, const char *path, struct k86_dirent *out, int max)
{
    return k86_call(SYS_FS_LIST, (uint32_t)vol, (uint32_t)path, (uint32_t)out, (uint32_t)max);
}

static inline int k86_create(int vol, const char *dir, const char *name, int is_dir)
{
    return k86_call(SYS_FS_CREATE, (uint32_t)vol, (uint32_t)dir, (uint32_t)name, (uint32_t)is_dir);
}

static inline int k86_rename(int vol, const char *dir, const char *from, const char *to)
{
    return k86_call(SYS_FS_RENAME, (uint32_t)vol, (uint32_t)dir, (uint32_t)from, (uint32_t)to);
}

/* Read a file from its start into buf (at most len bytes); the bytes read. */
static inline int k86_read(int vol, const char *dir, const char *name, void *buf, uint32_t len)
{
    return k86_call5(SYS_FS_READ, (uint32_t)vol, (uint32_t)dir, (uint32_t)name, (uint32_t)buf, len);
}

static inline int k86_delete(int vol, const char *dir, const char *name)
{
    return k86_call(SYS_FS_DELETE, (uint32_t)vol, (uint32_t)dir, (uint32_t)name, 0);
}

#endif

#include "kernel/lkx.h"
#include "kernel/errno.h"
#include "kernel/io.h"
#include "kernel/kern86_abi.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/ramdisk.h"
#include "kernel/string.h"
#include "kernel/user.h"
#include "kernel/vmm.h"
#include "kernel/desktop.h"
#include "kernel/wm.h"

#define MANIFEST_MAX 1024
#define PATH_MAX 56

/* Strings for the registered apps (struct app points into these). */
static struct {
    char name[24], icon[24], path[PATH_MAX];
    int order;              /* "order" in kerns.json: lower comes first (default 100) */
    int width, height, min_w, min_h, pinned;
    uint32_t size;
} found[APPS_MAX];
static int nfound;

static int strncmp_(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 1;
        if (!a[i])
            return 0;
    }
    return 0;
}

/* v1's tiny JSON reader: the string value of "key", or 0 if absent. Enough
 * for kerns.json's flat object of strings; escapes aren't supported. */
static int json_str(const char *json, const char *key, char *out, int max)
{
    int klen = (int)strlen(key);
    for (const char *p = json; *p; p++) {
        if (*p != '"' || strncmp_(p + 1, key, klen) || p[1 + klen] != '"')
            continue;
        p += 2 + klen;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
            p++;
        if (*p != '"')
            return 0;
        int n = 0;
        for (p++; *p && *p != '"' && n < max - 1; p++)
            out[n++] = *p;
        out[n] = '\0';
        return *p == '"' && n > 0;
    }
    return 0;
}

/* The whole-number value of "key", or `fallback` if absent. */
static int json_int(const char *json, const char *key, int fallback)
{
    int klen = (int)strlen(key);
    for (const char *p = json; *p; p++) {
        if (*p != '"' || strncmp_(p + 1, key, klen) || p[1 + klen] != '"')
            continue;
        p += 2 + klen;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')
            p++;
        if (*p < '0' || *p > '9')
            return fallback;
        int v = 0;
        while (*p >= '0' && *p <= '9' && v < 100000)
            v = v * 10 + (*p++ - '0');
        return v;
    }
    return fallback;
}

static int ends_with(const char *s, const char *tail)
{
    int n = (int)strlen(s), t = (int)strlen(tail);
    return n >= t && !strcmp(s + n - t, tail);
}

static void register_one(const char *manifest_name)
{
    const void *data;
    uint32_t size;
    char json[MANIFEST_MAX + 1], entry[24];
    if (nfound == APPS_MAX || ramdisk_find(manifest_name, &data, &size) || size > MANIFEST_MAX) {
        kprintf("lkx: %s: unreadable or too big\n", manifest_name);
        return;
    }
    memcpy(json, data, size);
    json[size] = '\0';
    typeof(found[0]) *f = &found[nfound];
    if (!json_str(json, "name", f->name, sizeof(f->name)) ||
        !json_str(json, "entry", entry, sizeof(entry))) {
        kprintf("lkx: %s: needs \"name\" and \"entry\"\n", manifest_name);
        return;
    }
    if (!json_str(json, "icon", f->icon, sizeof(f->icon)))
        f->icon[0] = '\0';
    /* The program sits next to its kerns.json. */
    int dir = (int)strlen(manifest_name) - (int)strlen("kerns.json");
    if (dir + (int)strlen(entry) >= PATH_MAX)
        return;
    memcpy(f->path, manifest_name, (size_t)dir);
    memcpy(f->path + dir, entry, strlen(entry) + 1);
    uint32_t prog_size;
    if (ramdisk_size(f->path, &prog_size)) {          /* read when it's opened */
        kprintf("lkx: %s: its program %s isn't in the ramdisk\n", manifest_name, f->path);
        return;
    }
    f->order = json_int(json, "order", 100);
    f->width = json_int(json, "width", 0);
    f->height = json_int(json, "height", 0);
    f->min_w = json_int(json, "min_width", 0);
    f->min_h = json_int(json, "min_height", 0);
    f->pinned = json_int(json, "pinned", 0);
    f->size = prog_size;
    nfound++;
}

void lkx_register_all(void)
{
    nfound = 0;
    for (int i = 0; i < ramdisk_count(); i++) {
        const char *n = ramdisk_name(i);
        if (n && !strncmp_(n, "apps/", 5) && ends_with(n, "/kerns.json"))
            register_one(n);
    }
    /* By "order", keeping the ramdisk's order for equal ones (insertion sort). */
    int idx[APPS_MAX];
    for (int i = 0; i < nfound; i++) {
        int j = i;
        while (j > 0 && found[idx[j - 1]].order > found[i].order) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = i;
    }
    for (int k = 0; k < nfound; k++) {
        typeof(found[0]) *f = &found[idx[k]];
        struct app a = { .name = f->name, .icon = f->icon, .lkx = f->path, .width = f->width,
                         .height = f->height, .min_width = f->min_w, .min_height = f->min_h,
                         .pinned = f->pinned };
        if (apps_add(&a) == 0)
            kprintf("lkx: %s (%s, %u KB)\n", f->name, f->path, f->size / 1024);
    }
    if (!nfound)
        kprintf("lkx: no apps in the ramdisk\n");
}

/* --- running apps: one process each, taking turns ---------------------------------- */

#define PROC_MAX WM_MAX

struct proc {
    int used;
    const struct app *app;
    struct wm_window *win;
    struct user_proc u;
    int started;            /* has run at least once */
    int waiting;            /* in SYS_WAIT_EVENT, for event_out */
    int close_sent;         /* its window asked it to close: its next wait ends it */
    uint32_t event_out;
    uint32_t free_before;   /* memory before it started (the stability tests) */
};

static struct proc procs[PROC_MAX];
static struct proc *running_proc;

int lkx_start(const struct app *a, struct wm_window *w)
{
    const void *data;
    uint32_t size;
    if (ramdisk_find(a->lkx, &data, &size) || size < sizeof(struct lkx_header)) {
        kprintf("lkx: %s: program missing\n", a->name);
        return -ENOENT;
    }
    struct lkx_header h;
    memcpy(&h, data, sizeof(h));
    if (h.magic != LKX_MAGIC || h.version != LKX_VERSION ||
        h.image_size > size - sizeof(h) || !h.image_size) {
        kprintf("lkx: %s: not a valid .lkx program\n", a->name);
        return -EINVAL;
    }
    struct proc *p = 0;
    for (int i = 0; i < PROC_MAX && !p; i++)
        if (!procs[i].used)
            p = &procs[i];
    if (!p)
        return -ENOMEM;
    memset(p, 0, sizeof(*p));
    p->free_before = pmm_free_frames();
    int err = user_create(&p->u, (const uint8_t *)data + sizeof(h), h.image_size, h.bss_size, h.entry);
    if (err) {
        kprintf("lkx: %s couldn't start (%s)\n", a->name, errno_name(err));
        return err;
    }
    p->used = 1;
    p->app = a;
    p->win = w;
    return 0;
}

static const char *exception_name(uint32_t v)
{
    static const char *const names[] = { "divide error", "debug", "NMI", "breakpoint", "overflow",
        "bound range", "invalid opcode", "no FPU", "double fault", "?", "invalid TSS",
        "segment not present", "stack fault", "general protection", "page fault" };
    return v < sizeof(names) / sizeof(names[0]) ? names[v] : "exception";
}

static void ended(struct proc *p)
{
    const struct app *a = p->app;
    struct wm_window *w = p->win;
    const struct user_result *res = &p->u.res;
    if (res->hung)
        kprintf("lkx: %s stopped responding and was stopped\n", a->name);
    else if (res->killed)
        kprintf("lkx: %s crashed (%s) and was stopped; the system carries on\n", a->name,
                exception_name(res->vector));
    else
        kprintf("lkx: %s exited (%d)\n", a->name, res->exit_code);
    user_destroy(&p->u);
    /* Everything an app had must come back (the stability tests check it). */
    kprintf("lkx: memory free %u KiB before, %u KiB after\n", p->free_before * 4, pmm_free_frames() * 4);
    p->used = 0;
    desktop_app_ended(a, w);
}

/* Hand p its next event (in its own address space): 1 if there was one. */
static int deliver(struct proc *p, uint32_t out)
{
    struct wm_event ev;
    if (!wm_poll_event(p->win, &ev))
        return 0;
    p->close_sent |= ev.type == WM_EVENT_CLOSE;
    *(struct k86_event *)out = (struct k86_event){
        .type = (int32_t)ev.type, .x = ev.x, .y = ev.y, .buttons = ev.buttons,
        .changed = ev.changed, .id = ev.id, .time_ms = ev.time_ms, .key = ev.key };
    return 1;
}

int lkx_schedule(void)
{
    int ran = 0;
    for (int i = 0; i < PROC_MAX; i++) {
        struct proc *p = &procs[i];
        if (!p->used || (p->started && !(p->waiting && wm_has_event(p->win))))
            continue;
        if (p->waiting) {               /* its SYS_WAIT_EVENT returns, with the event */
            uint32_t was = vmm_space_current();
            vmm_space_switch(p->u.pd);
            deliver(p, p->event_out);
            vmm_space_switch(was);
            p->u.frame.eax = 0;
            p->waiting = 0;
        }
        p->started = 1;
        running_proc = p;
        uint32_t flags;
        __asm__ volatile("pushf; pop %0" : "=r"(flags));
        int yielded = user_resume(&p->u);
        irq_restore(flags);             /* ring 3 always comes back with interrupts off */
        running_proc = 0;
        ran++;
        if (!yielded)
            ended(p);
    }
    return ran;
}

struct wm_window *lkx_window(void)
{
    return running_proc ? running_proc->win : 0;
}

void lkx_wait_event(struct int_frame *f)
{
    struct proc *p = running_proc;
    uint32_t out = f->ebx;
    if (user_check(out, sizeof(struct k86_event), 1)) {
        f->eax = (uint32_t)-EFAULT;
        return;
    }
    if (p->close_sent)
        user_exit(0);                   /* told to close, and back for more: done */
    if (deliver(p, out)) {
        f->eax = 0;
        return;
    }
    p->waiting = 1;                     /* sleep until its window has an event */
    p->event_out = out;
    user_yield(f);
}

int lkx_running(const struct app *a)
{
    for (int i = 0; i < PROC_MAX; i++)
        if (procs[i].used && procs[i].app == a)
            return 1;
    return 0;
}

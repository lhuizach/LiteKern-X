#include "kernel/lkx.h"
#include "kernel/errno.h"
#include "kernel/io.h"
#include "kernel/kern86_abi.h"
#include "kernel/printk.h"
#include "kernel/ramdisk.h"
#include "kernel/string.h"
#include "kernel/sys_app.h"
#include "kernel/user.h"

#define MANIFEST_MAX 1024
#define PATH_MAX 56

/* Strings for the registered apps (struct app points into these). */
static struct {
    char name[24], icon[24], path[PATH_MAX];
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
    const void *prog;
    uint32_t prog_size;
    if (ramdisk_find(f->path, &prog, &prog_size)) {
        kprintf("lkx: %s: its program %s isn't in the ramdisk\n", manifest_name, f->path);
        return;
    }
    struct app a = { .name = f->name, .icon = f->icon, .lkx = f->path };
    if (apps_add(&a) == 0) {
        nfound++;
        kprintf("lkx: %s (%s, %u KB)\n", f->name, f->path, prog_size / 1024);
    }
}

void lkx_register_all(void)
{
    nfound = 0;
    for (int i = 0; i < ramdisk_count(); i++) {
        const char *n = ramdisk_name(i);
        if (n && !strncmp_(n, "apps/", 5) && ends_with(n, "/kerns.json"))
            register_one(n);
    }
    if (!nfound)
        kprintf("lkx: no apps in the ramdisk\n");
}

static const char *exception_name(uint32_t v)
{
    static const char *const names[] = { "divide error", "debug", "NMI", "breakpoint", "overflow",
        "bound range", "invalid opcode", "no FPU", "double fault", "?", "invalid TSS",
        "segment not present", "stack fault", "general protection", "page fault" };
    return v < sizeof(names) / sizeof(names[0]) ? names[v] : "exception";
}

void lkx_run(const struct app *a)
{
    const void *data;
    uint32_t size;
    if (ramdisk_find(a->lkx, &data, &size) || size < sizeof(struct lkx_header)) {
        kprintf("lkx: %s: program missing\n", a->name);
        return;
    }
    struct lkx_header h;
    memcpy(&h, data, sizeof(h));
    if (h.magic != LKX_MAGIC || h.version != LKX_VERSION ||
        h.image_size > size - sizeof(h) || !h.image_size) {
        kprintf("lkx: %s: not a valid .lkx program\n", a->name);
        return;
    }
    sys_app_start(a->name);
    struct user_result res;
    uint32_t flags;
    __asm__ volatile("pushf; pop %0" : "=r"(flags));
    int err = user_exec_app((const uint8_t *)data + sizeof(h), h.image_size, h.bss_size, h.entry, &res);
    irq_restore(flags);         /* ring 3 always comes back with interrupts off */
    sys_app_end();
    if (err)
        kprintf("lkx: %s couldn't start (%s)\n", a->name, errno_name(err));
    else if (res.hung)
        kprintf("lkx: %s stopped responding and was stopped\n", a->name);
    else if (res.killed)
        kprintf("lkx: %s crashed (%s) and was stopped; the system carries on\n", a->name,
                exception_name(res.vector));
    else
        kprintf("lkx: %s exited (%d)\n", a->name, res.exit_code);
}

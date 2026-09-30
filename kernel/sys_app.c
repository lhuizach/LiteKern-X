#include "kernel/sys_app.h"
#include "kernel/errno.h"
#include "kernel/fat32.h"
#include "kernel/font.h"
#include "kernel/idle.h"
#include "kernel/kern86_abi.h"
#include "kernel/printk.h"
#include "kernel/storage.h"
#include "kernel/string.h"
#include "kernel/user.h"
#include "kernel/wm.h"

static int canvas_w, canvas_h;      /* 0 until SYS_WINDOW_OPEN */
static int close_sent;              /* the app has been told to close */

void sys_app_start(const char *name)
{
    (void)name;
    canvas_w = canvas_h = 0;
    close_sent = 0;
}

void sys_app_end(void)
{
    canvas_w = canvas_h = 0;
}

/* Copy a NUL-terminated string from the app, checking every byte's page. */
static int user_string(char *dst, uint32_t src, int max)
{
    for (int i = 0; i < max; i++) {
        if (user_check(src + (uint32_t)i, 1, 0))
            return -EFAULT;
        dst[i] = *(const char *)(src + (uint32_t)i);
        if (!dst[i])
            return 0;
    }
    return -EINVAL;                         /* too long */
}

/* --- window ------------------------------------------------------------------ */

static int window_open(uint32_t out)
{
    struct gfx_surface *c = wm_content();
    if (!wm_is_open())
        return -EINVAL;
    if (user_check(out, sizeof(struct k86_window), 1))
        return -EFAULT;
    int err = user_map(K86_CANVAS, (uint32_t)(c->w * c->h * 4));
    if (err)
        return err;
    canvas_w = c->w;
    canvas_h = c->h;
    *(struct k86_window *)out = (struct k86_window){ c->w, c->h, (uint32_t *)K86_CANVAS };
    return 0;
}

static int window_present(int x, int y, int w, int h)
{
    if (!canvas_w)
        return -EINVAL;
    struct gfx_rect r = gfx_rect_intersect((struct gfx_rect){ x, y, w, h },
                                           (struct gfx_rect){ 0, 0, canvas_w, canvas_h });
    if (gfx_rect_empty(r))
        return 0;
    /* The canvas pages were mapped by window_open and only the kernel unmaps
     * them, so they are still there. */
    struct gfx_surface canvas = { (uint32_t *)K86_CANVAS, canvas_w, canvas_h, canvas_w };
    gfx_blit(wm_content(), r.x, r.y, &canvas, r.x, r.y, r.w, r.h);
    wm_damage(r.x, r.y, r.w, r.h);
    wm_present();
    return 0;
}

static int window_header(uint32_t req)
{
    if (!wm_is_open())
        return -EINVAL;
    if (user_check(req, sizeof(struct k86_header), 0))
        return -EFAULT;
    struct k86_header h = *(const struct k86_header *)req;
    h.title[sizeof(h.title) - 1] = '\0';
    if (h.nbuttons < 0 || h.nbuttons > K86_MAX_BUTTONS)
        return -EINVAL;
    wm_clear_buttons();
    for (int i = 0; i < h.nbuttons; i++) {
        h.buttons[i].label[sizeof(h.buttons[i].label) - 1] = '\0';
        int icon = h.buttons[i].icon;
        if (icon < WM_ICON_NONE || icon > WM_ICON_UP)
            return -EINVAL;
        wm_add_button(h.buttons[i].side ? WM_RIGHT : WM_LEFT, (enum wm_icon)icon,
                      h.buttons[i].label, h.buttons[i].id);
    }
    wm_set_title(h.title);
    return 0;
}

/* Sleep in the kernel's input loop until the window has an event. After a
 * close has been delivered, the next call ends the app. */
static int wait_event(uint32_t out)
{
    if (user_check(out, sizeof(struct k86_event), 1))
        return -EFAULT;
    for (;;) {
        struct wm_event ev;
        if (close_sent || !wm_is_open())
            user_exit(0);
        if (wm_poll_event(&ev)) {
            close_sent = ev.type == WM_EVENT_CLOSE;
            *(struct k86_event *)out = (struct k86_event){
                .type = (int32_t)ev.type, .x = ev.x, .y = ev.y, .buttons = ev.buttons,
                .changed = ev.changed, .id = ev.id, .time_ms = ev.time_ms, .key = ev.key };
            return 0;
        }
        idle_step();                        /* input, the clock, the top bar; may sleep */
        __asm__ volatile("cli");            /* back to the syscall's own state */
    }
}

static int font(uint32_t out)
{
    if (user_check(out, K86_FONT_BYTES, 1))
        return -EFAULT;
    if (!font_available())
        return -ENODEV;
    memcpy((void *)out, font_glyph(0), K86_FONT_BYTES);
    return 0;
}

/* --- files ---------------------------------------------------------------------- */

static int volumes(uint32_t out, uint32_t max)
{
    if (max > STORAGE_MAX || user_check(out, max * sizeof(struct k86_volume), 1))
        return -EFAULT;
    struct k86_volume *v = (struct k86_volume *)out;
    for (int i = 0; i < storage_count() && (uint32_t)i < max; i++) {
        struct storage *s = storage_get(i);
        memset(&v[i], 0, sizeof(v[i]));
        memcpy(v[i].name, s->name, sizeof(v[i].name) - 1);
        v[i].supported = s->supported;
        v[i].read_only = s->supported && s->fat.read_only;
        if (s->why_not)
            for (int k = 0; s->why_not[k] && k < (int)sizeof(v[i].why_not) - 1; k++)
                v[i].why_not[k] = s->why_not[k];
        if (s->supported)
            v[i].free_kib = (uint32_t)(fat_free_bytes(&s->fat) / 1024);
    }
    return storage_count();
}

/* The volume, and the cluster of the folder at `upath` ("/", "/Documents"). */
static int resolve(uint32_t vol, uint32_t upath, struct fat_volume **v, uint32_t *dir)
{
    struct storage *s = storage_get((int)vol);
    char path[K86_PATH_MAX];
    if (!s || !s->supported)
        return -ENODEV;
    int err = user_string(path, upath, sizeof(path));
    if (err)
        return err;
    if (path[0] != '/')
        return -EINVAL;
    *v = &s->fat;
    *dir = fat_root(&s->fat);
    for (char *p = path + 1; *p;) {
        char *slash = p;
        while (*slash && *slash != '/')
            slash++;
        char saved = *slash;
        *slash = '\0';
        if (!strcmp(p, ".") || !strcmp(p, ".."))
            return -EINVAL;                 /* plain paths only */
        struct fat_entry e;
        if (*p) {
            if ((err = fat_find(*v, *dir, p, &e)))
                return err;
            if (!e.is_dir)
                return -EINVAL;
            *dir = e.cluster;
        }
        if (!saved)
            break;
        p = slash + 1;
    }
    return 0;
}

struct lister {
    struct k86_dirent *out;
    uint32_t max, n;
};

static int list_fn(void *ctx, const struct fat_entry *e)
{
    struct lister *l = ctx;
    if (l->n < l->max) {
        struct k86_dirent *d = &l->out[l->n];
        memset(d, 0, sizeof(*d));
        memcpy(d->name, e->name, sizeof(d->name) - 1);
        d->size = e->size;
        d->is_dir = e->is_dir;
        d->date = e->date;
        d->time = e->time;
    }
    l->n++;
    return 0;
}

static int fs_list(uint32_t vol, uint32_t upath, uint32_t out, uint32_t max)
{
    struct fat_volume *v;
    uint32_t dir;
    if (max > 4096 || user_check(out, max * sizeof(struct k86_dirent), 1))
        return -EFAULT;
    int err = resolve(vol, upath, &v, &dir);
    if (err)
        return err;
    struct lister l = { (struct k86_dirent *)out, max, 0 };
    err = fat_list(v, dir, list_fn, &l);
    return err < 0 ? err : (int)l.n;
}

static int fs_change(uint32_t nr, uint32_t vol, uint32_t upath, uint32_t uname, uint32_t arg)
{
    struct fat_volume *v;
    uint32_t dir;
    char name[K86_NAME_MAX], other[K86_NAME_MAX];
    int err = resolve(vol, upath, &v, &dir);
    if (!err)
        err = user_string(name, uname, sizeof(name));
    if (err)
        return err;
    if (nr == SYS_FS_CREATE)
        return fat_create(v, dir, name, arg != 0, 0);
    struct fat_entry e;
    if ((err = fat_find(v, dir, name, &e)))
        return err;
    if (nr == SYS_FS_DELETE)
        return fat_delete(v, &e);
    if ((err = user_string(other, arg, sizeof(other))))
        return err;
    return fat_rename(v, &e, other);
}

int sys_app(uint32_t nr, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    (void)a5;
    switch (nr) {
    case SYS_WINDOW_OPEN:    return window_open(a1);
    case SYS_WINDOW_PRESENT: return window_present((int)a1, (int)a2, (int)a3, (int)a4);
    case SYS_WINDOW_HEADER:  return window_header(a1);
    case SYS_WAIT_EVENT:     return wait_event(a1);
    case SYS_FONT:           return font(a1);
    case SYS_FS_VOLUMES:     return volumes(a1, a2);
    case SYS_FS_LIST:        return fs_list(a1, a2, a3, a4);
    case SYS_FS_CREATE:
    case SYS_FS_RENAME:
    case SYS_FS_DELETE:      return fs_change(nr, a1, a2, a3, a4);
    default:                 return -ENOSYS;
    }
}

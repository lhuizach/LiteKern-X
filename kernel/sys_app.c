#include "kernel/sys_app.h"
#include "kernel/errno.h"
#include "kernel/desktop.h"
#include "kernel/fat32.h"
#include "kernel/font.h"
#include "kernel/lkx.h"
#include "kernel/kern86_abi.h"
#include "kernel/printk.h"
#include "kernel/screensaver.h"
#include "kernel/storage.h"
#include "kernel/string.h"
#include "kernel/theme.h"
#include "kernel/user.h"
#include "kernel/wallpaper.h"
#include "kernel/wm.h"

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

/* The app's window was opened for it when it started: map its pixels in as
 * the canvas. The buffer stays the window's (shared): the app's teardown
 * doesn't free it. */
static int window_open(uint32_t out)
{
    struct wm_window *w = lkx_window();
    if (!w)
        return -EINVAL;
    if (user_check(out, sizeof(struct k86_window), 1))
        return -EFAULT;
    uint32_t bytes;
    uint32_t *buf = wm_buffer(w, &bytes);
    int err = user_map_shared(K86_CANVAS, (uint32_t)buf, bytes);
    if (err)
        return err;
    struct gfx_surface *c = wm_content(w);
    *(struct k86_window *)out = (struct k86_window){ c->w, c->h, (uint32_t *)K86_CANVAS, c->stride };
    return 0;
}

static int window_present(int x, int y, int w, int h)
{
    struct wm_window *win = lkx_window();
    if (!win || user_check(K86_CANVAS, 4, 1))
        return -EINVAL;                     /* no canvas yet */
    /* The app drew straight into the window's pixels: just show them. */
    wm_damage(win, x, y, w, h);
    wm_present();
    return 0;
}

static int window_header(uint32_t req)
{
    struct wm_window *w = lkx_window();
    if (!w)
        return -EINVAL;
    if (user_check(req, sizeof(struct k86_header), 0))
        return -EFAULT;
    struct k86_header h = *(const struct k86_header *)req;
    h.title[sizeof(h.title) - 1] = '\0';
    if (h.nbuttons < 0 || h.nbuttons > K86_MAX_BUTTONS)
        return -EINVAL;
    for (int i = 0; i < h.nbuttons; i++)
        if (h.buttons[i].icon < WM_ICON_NONE || h.buttons[i].icon > WM_ICON_UP)
            return -EINVAL;
    wm_clear_buttons(w);
    for (int i = 0; i < h.nbuttons; i++) {
        h.buttons[i].label[sizeof(h.buttons[i].label) - 1] = '\0';
        wm_add_button(w, h.buttons[i].side ? WM_RIGHT : WM_LEFT, (enum wm_icon)h.buttons[i].icon,
                      h.buttons[i].label, h.buttons[i].id);
    }
    wm_set_title(w, h.title);
    wm_present();
    return 0;
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

/* --- appearance (the Settings app) ------------------------------------------- */

static int theme_out(uint32_t out)
{
    if (user_check(out, sizeof(struct theme), 1))
        return -EFAULT;
    *(struct theme *)out = *theme_get();
    return 0;
}

static int appearance(uint32_t out)
{
    if (user_check(out, sizeof(struct k86_appearance), 1))
        return -EFAULT;
    struct k86_appearance *a = (struct k86_appearance *)out;
    memset(a, 0, sizeof(*a));
    a->style = theme_is_light();
    a->accent = theme_accent_index();
    const char *cur = wallpaper_current();
    for (int i = 0; cur[i] && i < 31; i++)
        a->wallpaper[i] = cur[i];
    uint32_t colour;
    const char *name;
    for (int i = 0; i < K86_MAX_ACCENTS && (name = theme_accent(i, &colour)); i++) {
        for (int k = 0; name[k] && k < 11; k++)
            a->accents[i].name[k] = name[k];
        a->accents[i].colour = colour;
        a->naccents++;
    }
    a->nwallpapers = wallpaper_list(a->wallpapers, K86_MAX_WALLPAPERS);
    a->screensaver_s = (int32_t)screensaver_timeout();
    return 0;
}

static int screensaver_set(uint32_t seconds, uint32_t preview)
{
    if (preview) {                      /* show it now; any input ends it */
        kprintf("appearance: screen saver preview\n");
        screensaver_start();
        return 0;
    }
    if (seconds > 3600)
        return -EINVAL;
    screensaver_set_timeout(seconds);
    if (seconds)
        kprintf("appearance: screen saver after %u s\n", seconds);
    else
        kprintf("appearance: screen saver off\n");
    return 0;
}

static int appearance_set(int style, int accent, uint32_t uname)
{
    char name[32];
    int err = 0;
    if (uname && (err = user_string(name, uname, sizeof(name))))
        return err;
    int old_light = theme_is_light();
    theme_set(style, accent);
    if (uname)
        err = wallpaper_set(name);
    else if (theme_is_light() != old_light)
        wallpaper_refresh();                /* its light or dark variant */
    kprintf("appearance: %s, accent %s, wallpaper %s\n", theme_is_light() ? "light" : "dark",
            theme_accent(theme_accent_index(), 0),
            wallpaper_current()[0] ? wallpaper_current() : "none");
    desktop_refresh();
    wm_theme_changed();
    return err;
}

static int wallpaper_thumbnail(uint32_t uname, uint32_t out, int w, int h)
{
    char name[32];
    if (w <= 0 || h <= 0 || w > 256 || h > 160)
        return -EINVAL;
    if (user_check(out, (uint32_t)(w * h * 4), 1))
        return -EFAULT;
    int err = user_string(name, uname, sizeof(name));
    return err ? err : wallpaper_thumb(name, (uint32_t *)out, w, h);
}

static int fs_read(uint32_t vol, uint32_t upath, uint32_t uname, uint32_t buf, uint32_t len)
{
    struct fat_volume *v;
    uint32_t dir;
    char name[K86_NAME_MAX];
    if (len > 1024 * 1024 || user_check(buf, len, 1))
        return -EFAULT;
    int err = resolve(vol, upath, &v, &dir);
    if (!err)
        err = user_string(name, uname, sizeof(name));
    if (err)
        return err;
    struct fat_entry e;
    if ((err = fat_find(v, dir, name, &e)))
        return err;
    if (e.is_dir)
        return -EINVAL;
    return fat_read(v, &e, 0, (void *)buf, len);
}

int sys_app(uint32_t nr, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    switch (nr) {
    case SYS_WINDOW_OPEN:    return window_open(a1);
    case SYS_WINDOW_PRESENT: return window_present((int)a1, (int)a2, (int)a3, (int)a4);
    case SYS_WINDOW_HEADER:  return window_header(a1);
    case SYS_FONT:           return font(a1);
    case SYS_FS_VOLUMES:     return volumes(a1, a2);
    case SYS_FS_LIST:        return fs_list(a1, a2, a3, a4);
    case SYS_FS_CREATE:
    case SYS_FS_RENAME:
    case SYS_FS_DELETE:      return fs_change(nr, a1, a2, a3, a4);
    case SYS_THEME_GET:      return theme_out(a1);
    case SYS_APPEARANCE:     return appearance(a1);
    case SYS_APPEARANCE_SET: return appearance_set((int)a1, (int)a2, a3);
    case SYS_WALLPAPER_THUMB: return wallpaper_thumbnail(a1, a2, (int)a3, (int)a4);
    case SYS_FS_READ:        return fs_read(a1, a2, a3, a4, a5);
    case SYS_SCREENSAVER_SET: return screensaver_set(a1, a2);
    default:                 return -ENOSYS;
    }
}

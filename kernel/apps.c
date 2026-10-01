#include "kernel/apps.h"
#include "kernel/console.h"
#include "kernel/errno.h"
#include "kernel/lkx.h"
#include "kernel/font.h"
#include "kernel/icon.h"
#include "kernel/printk.h"
#include "kernel/status.h"
#include "kernel/theme.h"

#define LOG_FG 0xc8d0dc     /* the console's usual text colour */

/* --- key logging (the desktop and the Log app) ------------------------- */

static int console_key(const struct key_event *ev)
{
    int page = (int)console_rows() - 1;
    switch (ev->key) {
    case KEY_PAGEUP:   if (ev->pressed) console_scroll(page);    return 1;
    case KEY_PAGEDOWN: if (ev->pressed) console_scroll(-page);   return 1;
    case KEY_HOME:     if (ev->pressed) console_scroll(100000);  return 1;
    case KEY_END:      if (ev->pressed) console_scroll(-100000); return 1;
    }
    return 0;
}

void log_key(const struct key_event *ev)
{
    if (console_key(ev) || !ev->pressed)
        return;
    kprintf("kbd: key 0x%03x", ev->key);
    if (ev->ascii >= 0x20 && ev->ascii < 0x7f)
        kprintf(" '%c'", ev->ascii);
    else if (ev->ascii)
        kprintf(" ascii 0x%02x", ev->ascii);
    if (ev->mods)
        kprintf(" mods 0x%x", ev->mods);
    kprintf("\n");
}

/* --- Log: the boot log, in a window ------------------------------------- */

static void log_open(void)
{
    const struct theme *t = theme_get();
    console_set_colours(t->light ? t->fg : LOG_FG, t->view_bg);
    console_set_area(theme_get()->topbar_h + theme_get()->headerbar_h);
    console_set_visible(1);
}

static void log_event(const struct wm_event *ev)
{
    if (ev->type == WM_EVENT_KEY)
        log_key(&ev->key);
}

static void log_close(void)
{
    console_set_visible(0);
    console_set_area(0);
    status_show(STATUS_READY);      /* the full-screen log's own colours again */
}

struct app builtin_apps[APPS_MAX];
int builtin_app_count;

int apps_add(const struct app *a)
{
    if (builtin_app_count == APPS_MAX)
        return -ENOMEM;
    builtin_apps[builtin_app_count++] = *a;
    return 0;
}

void apps_init(void)
{
    static const struct app log_app = { "Log", "log", log_open, log_event, log_close, 0 };
    builtin_app_count = 0;
    lkx_register_all();
    apps_add(&log_app);
}

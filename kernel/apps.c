#include "kernel/apps.h"
#include "kernel/console.h"
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
    console_set_colours(LOG_FG, theme_get()->view_bg);
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

const struct app builtin_apps[] = {
    { "Files", "files", files_open, files_event, 0 },
    { "Log", "log", log_open, log_event, log_close },
};
const int builtin_app_count = sizeof(builtin_apps) / sizeof(builtin_apps[0]);

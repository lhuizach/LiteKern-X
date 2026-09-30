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

/* --- Files: a placeholder until storage (Phase 2 §5a) ------------------- */

static void centred(struct gfx_surface *s, int y, const char *text, uint32_t colour, int bold)
{
    int x = (s->w - gfx_text_width(text)) / 2;
    gfx_text(s, x, y, text, colour, GFX_TRANSPARENT);
    if (bold)
        gfx_text(s, x + 1, y, text, colour, GFX_TRANSPARENT);
}

/* An Adwaita "status page": icon, title, explanation, centred. */
static void files_open(void)
{
    const struct theme *t = theme_get();
    struct gfx_surface *c = wm_content();
    const struct icon *ic = icon_find("files");
    int y = c->h / 2 - 70;
    if (ic)
        gfx_blit_alpha(c, (c->w - ic->w) / 2, y, ic->px, ic->w, ic->h, ic->w);
    centred(c, y + 48 + 20, "No disks yet", t->fg, 1);
    centred(c, y + 48 + 48, "Browsing the USB stick and the internal disk", t->fg_dim, 0);
    centred(c, y + 48 + 48 + FONT_H + 2, "arrives with storage support.", t->fg_dim, 0);
    wm_damage(0, 0, c->w, c->h);
    wm_present();
}

/* --- Log: the boot log, in a window ------------------------------------- */

static void log_open(void)
{
    console_set_colours(LOG_FG, theme_get()->view_bg);
    console_set_area(theme_get()->headerbar_h);
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
    { "Files", "files", files_open, 0, 0 },
    { "Log", "log", log_open, log_event, log_close },
};
const int builtin_app_count = sizeof(builtin_apps) / sizeof(builtin_apps[0]);

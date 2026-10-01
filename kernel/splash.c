#include "kernel/splash.h"
#include "kernel/console.h"
#include "kernel/icon.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/string.h"
#include "kernel/theme.h"
#include "defaults.h"           /* DEFAULT_SPLASH */

#define BAR_W 220
#define BAR_H 6
#define BG    0x000000          /* like Fedora's boot screen */

/* build/gen/splash.c (assets/splash.json: the 128x128 logo) */
extern const struct icon splash_images[];
extern const int splash_images_count;

static int active, shown_percent;
static struct gfx_rect bar;

static void draw_bar(int percent)
{
    struct gfx_surface *s = screen_surface();
    uint32_t track = gfx_mix(0xffffff, BG, 46);
    gfx_fill_rect(s, bar.x - 2, bar.y - 2, bar.w + 4, bar.h + 4, BG);
    gfx_fill_round_rect(s, bar.x, bar.y, bar.w, bar.h, BAR_H / 2, track);
    int w = bar.w * percent / 100;
    if (w >= BAR_H)
        gfx_fill_round_rect(s, bar.x, bar.y, w, bar.h, BAR_H / 2, theme_get()->accent_bg);
    screen_damage(bar.x - 2, bar.y - 2, bar.w + 4, bar.h + 4);
    screen_present();
}

void splash_start(void)
{
    if (strcmp(DEFAULT_SPLASH, "logo") || !screen_ready() || !splash_images_count) {
        kprintf("splash: none (the boot log)\n");
        return;
    }
    struct gfx_surface *s = screen_surface();
    const struct icon *logo = &splash_images[0];
    console_set_visible(0);         /* still recorded: the Log app shows it */
    gfx_fill_rect(s, 0, 0, s->w, s->h, BG);
    int lx = (s->w - logo->w) / 2, ly = (s->h - logo->h) / 2 - 30;
    gfx_blit_alpha(s, lx, ly, logo->px, logo->w, logo->h, logo->w);
    bar = (struct gfx_rect){ (s->w - BAR_W) / 2, ly + logo->h + 48, BAR_W, BAR_H };
    screen_damage_all();
    active = 1;
    shown_percent = -1;
    splash_progress(10);
    kprintf("splash: logo\n");
}

void splash_progress(int percent)
{
    if (!active || percent <= shown_percent)
        return;
    shown_percent = percent > 100 ? 100 : percent;
    draw_bar(shown_percent);
}

int splash_active(void)
{
    return active;
}

void splash_end(void)
{
    if (active)
        splash_progress(100);
    active = 0;
}

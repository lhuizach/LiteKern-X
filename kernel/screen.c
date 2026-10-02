#include "kernel/screen.h"
#include "kernel/errno.h"
#include "kernel/fb.h"
#include "kernel/pmm.h"

static device_t *fb;
static struct gfx_surface back;
static struct gfx_rect damage[SCREEN_MAX_DAMAGE];
static int ndamage;
static const uint32_t *overlay_px;      /* NULL: no overlay */
static struct gfx_rect overlay;

int screen_init(device_t *dev)
{
    struct fb_info info;
    if (dev_ioctl(dev, FB_GET_INFO, &info) < 0)
        return -ENODEV;
    uint32_t bytes = info.width * info.height * 4;
    uint32_t phys = pmm_alloc_contiguous((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (!phys)
        return -ENOMEM;
    fb = dev;
    back = (struct gfx_surface){ (uint32_t *)phys, (int)info.width, (int)info.height,
                                 (int)info.width };
    ndamage = 0;
    return 0;
}

int screen_ready(void)
{
    return fb != 0;
}

struct gfx_surface *screen_surface(void)
{
    return &back;
}

/* Overlapping, or sharing an edge: merging costs nothing extra. */
static int touches(struct gfx_rect a, struct gfx_rect b)
{
    return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;
}

void screen_damage(int x, int y, int w, int h)
{
    struct gfx_rect r = gfx_rect_intersect((struct gfx_rect){ x, y, w, h },
                                           (struct gfx_rect){ 0, 0, back.w, back.h });
    if (gfx_rect_empty(r))
        return;
    /* Absorb every rectangle r touches; a grown r may touch more, so rescan. */
    for (int i = 0; i < ndamage;) {
        if (touches(r, damage[i])) {
            r = gfx_rect_union(r, damage[i]);
            damage[i] = damage[--ndamage];
            i = 0;
        } else {
            i++;
        }
    }
    if (ndamage == SCREEN_MAX_DAMAGE) {
        for (int i = 0; i < ndamage; i++)
            r = gfx_rect_union(r, damage[i]);
        ndamage = 0;
    }
    damage[ndamage++] = r;
}

void screen_damage_all(void)
{
    ndamage = 0;
    screen_damage(0, 0, back.w, back.h);
}

/* Copy one rectangle of the back buffer, unchanged, to the display. */
static uint32_t copy_out(struct gfx_rect r)
{
    if (gfx_rect_empty(r))
        return 0;
    struct fb_blit b = { (uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.w, (uint32_t)r.h,
                         back.px + r.y * back.stride + r.x, (uint32_t)back.stride };
    dev_ioctl(fb, FB_BLIT, &b);
    return (uint32_t)(r.w * r.h);
}

/* Copy a rectangle that lies inside the overlay: back buffer + overlay on top,
 * composed in a scratch buffer so each display pixel is written once. */
static uint32_t copy_out_with_overlay(struct gfx_rect r)
{
    static uint32_t scratch[SCREEN_OVERLAY_MAX * SCREEN_OVERLAY_MAX];
    struct gfx_surface tmp = { scratch, r.w, r.h, r.w };
    gfx_blit(&tmp, 0, 0, &back, r.x, r.y, r.w, r.h);
    gfx_blit_alpha(&tmp, overlay.x - r.x, overlay.y - r.y, overlay_px, overlay.w, overlay.h,
                   overlay.w);
    struct fb_blit b = { (uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.w, (uint32_t)r.h, scratch,
                         (uint32_t)r.w };
    dev_ioctl(fb, FB_BLIT, &b);
    return (uint32_t)(r.w * r.h);
}

uint32_t screen_present(void)
{
    uint32_t pixels = 0;
    /* The areas with the cursor in them first, then the rest: when it jumps
     * (a fast move), it appears in its new place before it's wiped from the
     * old one, so the display never catches a moment with no cursor at all
     * (it blinked out of sight in VirtualBox). */
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < ndamage; i++) {
        struct gfx_rect r = damage[i];
        struct gfx_rect o = overlay_px ? gfx_rect_intersect(r, overlay) : (struct gfx_rect){ 0, 0, 0, 0 };
        if (gfx_rect_empty(o) != pass)
            continue;
        if (gfx_rect_empty(o)) {
            pixels += copy_out(r);
            continue;
        }
        pixels += copy_out_with_overlay(o);     /* the cursor first, then the bands of r
                                                 * above, below, left and right of it */
        pixels += copy_out((struct gfx_rect){ r.x, r.y, r.w, o.y - r.y });
        pixels += copy_out((struct gfx_rect){ r.x, o.y + o.h, r.w, r.y + r.h - (o.y + o.h) });
        pixels += copy_out((struct gfx_rect){ r.x, o.y, o.x - r.x, o.h });
        pixels += copy_out((struct gfx_rect){ o.x + o.w, o.y, r.x + r.w - (o.x + o.w), o.h });
    }
    ndamage = 0;
    return pixels;
}

void screen_set_overlay(const uint32_t *argb, int w, int h, int x, int y)
{
    if (w > SCREEN_OVERLAY_MAX || h > SCREEN_OVERLAY_MAX)
        return;
    if (overlay_px)
        screen_damage(overlay.x, overlay.y, overlay.w, overlay.h);
    overlay_px = argb;
    overlay = (struct gfx_rect){ x, y, w, h };
    if (overlay_px)
        screen_damage(x, y, w, h);
}

int screen_damage_count(void)
{
    return ndamage;
}

struct gfx_rect screen_damage_rect(int i)
{
    return damage[i];
}

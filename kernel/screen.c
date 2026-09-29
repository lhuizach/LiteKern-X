#include "kernel/screen.h"
#include "kernel/errno.h"
#include "kernel/fb.h"
#include "kernel/pmm.h"

static device_t *fb;
static struct gfx_surface back;
static struct gfx_rect damage[SCREEN_MAX_DAMAGE];
static int ndamage;

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

uint32_t screen_present(void)
{
    uint32_t pixels = 0;
    for (int i = 0; i < ndamage; i++) {
        struct gfx_rect r = damage[i];
        struct fb_blit b = { (uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.w, (uint32_t)r.h,
                             back.px + r.y * back.stride + r.x, (uint32_t)back.stride };
        dev_ioctl(fb, FB_BLIT, &b);
        pixels += (uint32_t)(r.w * r.h);
    }
    ndamage = 0;
    return pixels;
}

int screen_damage_count(void)
{
    return ndamage;
}

struct gfx_rect screen_damage_rect(int i)
{
    return damage[i];
}

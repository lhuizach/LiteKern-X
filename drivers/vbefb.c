/* LiteKern X — VBE linear framebuffer display driver (Phase 1 §6).
 * Legacy device "fb0"; legacy_arg points at the kernel's boot_info, whose
 * fb_* fields describe the mode stage 2 set. Interface: kernel/fb.h.
 *
 * Only 32 bpp linear framebuffers, as stage 2 only ever picks those. No
 * mode setting, no acceleration (GMA 950 native modesetting is a Non-Goal).
 * On init it asks for the framebuffer to be write-combining (kernel/mtrr.h);
 * FB_GET_INFO reports whether that worked. */
#include "boot/bootinfo.h"
#include "drivers/builtin.h"
#include "kernel/errno.h"
#include "kernel/fb.h"
#include "kernel/mtrr.h"
#include "kernel/vmm.h"

struct vbefb {
    volatile uint8_t *base;
    uint32_t width, height, pitch, phys;
    uint32_t wc_status, wc_base, wc_len;
};

static struct vbefb fb;     /* one display */

static int vbefb_init(device_t *dev)
{
    const struct boot_info *bi = (const struct boot_info *)dev->legacy_arg;
    if (!bi || !(bi->flags & BI_FLAG_FB) || bi->fb_bpp != 32)
        return -ENODEV;
    if (!bi->fb_width || !bi->fb_height || bi->fb_pitch < bi->fb_width * 4)
        return -ENODEV;
    /* The whole framebuffer must be mapped (kernel/vmm.c maps it). */
    uint32_t last = bi->fb_addr + bi->fb_pitch * bi->fb_height - 1;
    if (!(vmm_lookup(bi->fb_addr) & PTE_PRESENT) || !(vmm_lookup(last) & PTE_PRESENT))
        return -ENODEV;

    fb.base = (volatile uint8_t *)bi->fb_addr;
    fb.phys = bi->fb_addr;
    fb.width = bi->fb_width;
    fb.height = bi->fb_height;
    fb.pitch = bi->fb_pitch;
    /* Best effort: a framebuffer that stays uncached is slow, not broken. */
    fb.wc_status = mtrr_set_write_combining(fb.phys, fb.pitch * fb.height, bi,
                                            &fb.wc_base, &fb.wc_len);
    dev->priv = &fb;
    return 0;
}

/* Clip (x, y, *w, *h) to the screen. Returns 0 if nothing is left. */
static int clip(uint32_t x, uint32_t y, uint32_t *w, uint32_t *h)
{
    if (x >= fb.width || y >= fb.height || !*w || !*h)
        return 0;
    if (*w > fb.width - x)
        *w = fb.width - x;
    if (*h > fb.height - y)
        *h = fb.height - y;
    return 1;
}

static int fill_rect(const struct fb_rect *r)
{
    uint32_t w = r->w, h = r->h;
    if (!clip(r->x, r->y, &w, &h))
        return 0;
    for (uint32_t row = 0; row < h; row++) {
        volatile uint32_t *p = (volatile uint32_t *)(fb.base + (r->y + row) * fb.pitch) + r->x;
        for (uint32_t col = 0; col < w; col++)
            p[col] = r->colour;
    }
    return 0;
}

static int blit(const struct fb_blit *b)
{
    if (!b->pixels || b->stride < b->w)
        return -EINVAL;
    uint32_t w = b->w, h = b->h;
    if (!clip(b->x, b->y, &w, &h))
        return 0;
    for (uint32_t row = 0; row < h; row++) {
        volatile uint32_t *dst = (volatile uint32_t *)(fb.base + (b->y + row) * fb.pitch) + b->x;
        const uint32_t *src = b->pixels + row * b->stride;
        for (uint32_t col = 0; col < w; col++)
            dst[col] = src[col];
    }
    return 0;
}

static int vbefb_ioctl(device_t *dev, unsigned cmd, void *arg)
{
    (void)dev;
    if (!arg)
        return -EINVAL;
    switch (cmd) {
    case FB_GET_INFO: {
        struct fb_info *info = arg;
        info->width = fb.width;
        info->height = fb.height;
        info->pitch = fb.pitch;
        info->bpp = 32;
        info->phys_addr = fb.phys;
        info->wc_status = fb.wc_status;
        info->wc_base = fb.wc_base;
        info->wc_len = fb.wc_len;
        return 0;
    }
    case FB_FILL_RECT:
        return fill_rect(arg);
    case FB_BLIT:
        return blit(arg);
    default:
        return -ENOSYS;
    }
}

const driver_t vbefb_driver = {
    .name = "vbefb",
    .pci_ids = NULL,        /* the mode comes from the BIOS, not a PCI device */
    .init = vbefb_init,
    .read = driver_nosys_read,
    .write = driver_nosys_write,
    .ioctl = vbefb_ioctl,
    .shutdown = driver_noop_shutdown,
};

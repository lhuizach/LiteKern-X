#include "kernel/wallpaper.h"
#include "kernel/errno.h"
#include "kernel/inflate.h"
#include "kernel/pmm.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/theme.h"
#include "kernel/timing.h"

/* kernel/wallpaper_data.asm: the packed image. */
extern const uint8_t wallpaper_data[];
extern const uint32_t wallpaper_size;

struct __attribute__((packed)) header {
    char magic[4];              /* "LKXW" */
    uint16_t w, h;
    uint32_t raw_len, packed_len, fnv;
    uint32_t top, bottom, left, right;
};

static struct gfx_surface image;    /* screen-sized, or px = 0 for the plain colour */

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p,
        pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Undo PNG-style filters in place (bpp 3). Returns the FNV-1a of the pixels,
 * or 0 with *ok = 0 on a bad filter type. */
static uint32_t unfilter(uint8_t *raw, int w, int h, int *ok)
{
    uint32_t fnv = 0x811c9dc5u;
    int stride = 1 + 3 * w;
    *ok = 1;
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + y * stride + 1, type = row[-1];
        const uint8_t *prev = y ? raw + (y - 1) * stride + 1 : 0;
        for (int i = 0; i < 3 * w; i++) {
            uint8_t a = i >= 3 ? row[i - 3] : 0, b = prev ? prev[i] : 0,
                    c = prev && i >= 3 ? prev[i - 3] : 0;
            switch (type) {
            case 0: break;
            case 1: row[i] = (uint8_t)(row[i] + a); break;
            case 2: row[i] = (uint8_t)(row[i] + b); break;
            case 3: row[i] = (uint8_t)(row[i] + ((a + b) >> 1)); break;
            case 4: row[i] = (uint8_t)(row[i] + paeth(a, b, c)); break;
            default: *ok = 0; return 0;
            }
            fnv = (fnv ^ row[i]) * 0x01000193u;
        }
    }
    return fnv;
}

static void free_frames(uint32_t phys, uint32_t bytes)
{
    for (uint32_t off = 0; off < bytes; off += PAGE_SIZE)
        pmm_free(phys + off);
}

int wallpaper_init(void)
{
    const struct header *hd = (const struct header *)wallpaper_data;
    struct gfx_surface *scr = screen_surface();
    uint32_t t0 = uptime_ms();
    if (wallpaper_size < sizeof(*hd) || hd->magic[0] != 'L' || hd->magic[1] != 'K' ||
        hd->magic[2] != 'X' || hd->magic[3] != 'W' ||
        hd->packed_len > wallpaper_size - sizeof(*hd) ||
        hd->raw_len != (uint32_t)hd->h * (1 + 3u * hd->w) || !hd->w || !hd->h) {
        kprintf("wallpaper: none built in; plain colour\n");
        return -ENOENT;
    }

    uint32_t raw_bytes = (hd->raw_len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t img_bytes = ((uint32_t)scr->w * scr->h * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t raw = pmm_alloc_contiguous(raw_bytes / PAGE_SIZE);
    uint32_t img = pmm_alloc_contiguous(img_bytes / PAGE_SIZE);
    if (!raw || !img) {
        if (raw)
            free_frames(raw, raw_bytes);
        if (img)
            free_frames(img, img_bytes);
        kprintf("wallpaper: out of memory; plain colour\n");
        return -ENOMEM;
    }

    int ok = 0, n = inflate_raw(wallpaper_data + sizeof(*hd), hd->packed_len, (uint8_t *)raw,
                                hd->raw_len);
    uint32_t fnv = n == (int)hd->raw_len ? unfilter((uint8_t *)raw, hd->w, hd->h, &ok) : 0;
    if (n != (int)hd->raw_len || !ok || fnv != hd->fnv) {
        kprintf("wallpaper: corrupt (inflate %d, checksum %s); plain colour\n", n,
                ok && fnv != hd->fnv ? "wrong" : "not checked");
        free_frames(raw, raw_bytes);
        free_frames(img, img_bytes);
        return -EINVAL;
    }

    /* Centre it; the edge colours fill whatever the image doesn't cover.
     * (A screen smaller than the image shows its middle.) */
    image = (struct gfx_surface){ (uint32_t *)img, scr->w, scr->h, scr->w };
    int ox = (scr->w - hd->w) / 2, oy = (scr->h - hd->h) / 2;
    gfx_fill_rect(&image, 0, 0, scr->w, oy, hd->top);
    gfx_fill_rect(&image, 0, oy + hd->h, scr->w, scr->h - oy - hd->h, hd->bottom);
    gfx_fill_rect(&image, 0, 0, ox, scr->h, hd->left);
    gfx_fill_rect(&image, ox + hd->w, 0, scr->w - ox - hd->w, scr->h, hd->right);
    int stride = 1 + 3 * hd->w;
    for (int y = 0; y < hd->h; y++) {
        int sy = oy + y;
        if (sy < 0 || sy >= scr->h)
            continue;
        const uint8_t *p = (const uint8_t *)raw + y * stride + 1;
        for (int x = 0; x < hd->w; x++, p += 3) {
            int sx = ox + x;
            if (sx >= 0 && sx < scr->w)
                image.px[sy * image.stride + sx] = (uint32_t)p[0] << 16 | p[1] << 8 | p[2];
        }
    }
    free_frames(raw, raw_bytes);
    kprintf("wallpaper: %ux%u, %u KB packed, unpacked and checked in %u ms\n", hd->w, hd->h,
            hd->packed_len / 1024, uptime_ms() - t0);
    return 0;
}

void wallpaper_draw(int x, int y, int w, int h, uint32_t dim)
{
    struct gfx_surface *scr = screen_surface();
    if (!image.px) {
        uint32_t c = theme_get()->desktop_bg;
        gfx_fill_rect(scr, x, y, w, h, dim ? gfx_mix(0, c, dim) : c);
        return;
    }
    gfx_blit(scr, x, y, &image, x, y, w, h);
    if (dim)
        gfx_darken(scr, x, y, w, h, dim);
}

uint32_t wallpaper_corner(void)
{
    return image.px ? image.px[(image.h - 1) * image.stride + image.w - 1] : theme_get()->desktop_bg;
}

#include "kernel/wallpaper.h"
#include "kernel/errno.h"
#include "kernel/inflate.h"
#include "kernel/pmm.h"
#include "kernel/ramdisk.h"
#include "kernel/string.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/theme.h"
#include "kernel/timing.h"

struct __attribute__((packed)) header {
    char magic[4];              /* "LKXW" */
    uint16_t w, h;
    uint32_t raw_len, packed_len, fnv;
    uint32_t top, bottom, left, right;
};

static struct gfx_surface image;    /* screen-sized, or px = 0 for the plain colour */
static int shown;                   /* image holds a wallpaper (else: the plain colour) */
static char current[32];            /* its name, without "-light" */

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

/* Unpack wallpapers/<name>.lkxw into a fresh buffer of unfiltered rows
 * (1 filter byte + 3 * w bytes each). The caller frees it (free_frames). */
static int unpack(const char *name, struct header *hd, uint32_t *raw, uint32_t *raw_bytes)
{
    char path[64] = "wallpapers/";
    int n = (int)strlen(path);
    for (int i = 0; name[i] && n < (int)sizeof(path) - 6; i++)
        path[n++] = name[i];
    memcpy(path + n, ".lkxw", 6);
    const void *data;
    uint32_t size;
    if (ramdisk_find(path, &data, &size))
        return -ENOENT;
    memcpy(hd, data, sizeof(*hd));
    if (size < sizeof(*hd) || memcmp(hd->magic, "LKXW", 4) || hd->packed_len > size - sizeof(*hd) ||
        !hd->w || !hd->h || hd->raw_len != (uint32_t)hd->h * (1 + 3u * hd->w))
        return -EINVAL;
    *raw_bytes = (hd->raw_len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    *raw = pmm_alloc_contiguous(*raw_bytes / PAGE_SIZE);
    if (!*raw)
        return -ENOMEM;
    int ok = 0, got = inflate_raw((const uint8_t *)data + sizeof(*hd), hd->packed_len,
                                  (uint8_t *)*raw, hd->raw_len);
    uint32_t fnv = got == (int)hd->raw_len ? unfilter((uint8_t *)*raw, hd->w, hd->h, &ok) : 0;
    if (got != (int)hd->raw_len || !ok || fnv != hd->fnv) {
        kprintf("wallpaper: %s is corrupt (inflate %d, checksum %s)\n", name, got,
                ok && fnv != hd->fnv ? "wrong" : "not checked");
        free_frames(*raw, *raw_bytes);
        return -EINVAL;
    }
    return 0;
}

int wallpaper_init(const char *name)
{
    struct gfx_surface *scr = screen_surface();
    uint32_t bytes = ((uint32_t)scr->w * scr->h * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t img = pmm_alloc_contiguous(bytes / PAGE_SIZE);
    if (!img) {
        kprintf("wallpaper: out of memory; plain colour\n");
        return -ENOMEM;
    }
    image = (struct gfx_surface){ (uint32_t *)img, scr->w, scr->h, scr->w };
    int err = wallpaper_set(name);
    if (err)
        kprintf("wallpaper: %s unavailable; plain colour\n", name);
    return err;
}

int wallpaper_set(const char *name)
{
    struct header hd;
    uint32_t raw, raw_bytes, t0 = uptime_ms();
    if (!image.px)
        return -ENODEV;
    /* The light style uses "<name>-light" where there is one. */
    char variant[40];
    int n = 0;
    for (; name[n] && n < 30; n++)
        variant[n] = name[n];
    memcpy(variant + n, "-light", 7);
    int err = -ENOENT;
    if (theme_is_light())
        err = unpack(variant, &hd, &raw, &raw_bytes);
    if (err)
        err = unpack(name, &hd, &raw, &raw_bytes);
    if (err)
        return err;

    /* Centre it; the edge colours fill whatever the image doesn't cover.
     * (A screen smaller than the image shows its middle.) */
    struct gfx_surface *scr = screen_surface();
    int ox = (scr->w - hd.w) / 2, oy = (scr->h - hd.h) / 2;
    gfx_fill_rect(&image, 0, 0, scr->w, oy, hd.top);
    gfx_fill_rect(&image, 0, oy + hd.h, scr->w, scr->h - oy - hd.h, hd.bottom);
    gfx_fill_rect(&image, 0, 0, ox, scr->h, hd.left);
    gfx_fill_rect(&image, ox + hd.w, 0, scr->w - ox - hd.w, scr->h, hd.right);
    int stride = 1 + 3 * hd.w;
    for (int y = 0; y < hd.h; y++) {
        int sy = oy + y;
        if (sy < 0 || sy >= scr->h)
            continue;
        const uint8_t *p = (const uint8_t *)raw + y * stride + 1;
        for (int x = 0; x < hd.w; x++, p += 3) {
            int sx = ox + x;
            if (sx >= 0 && sx < scr->w)
                image.px[sy * image.stride + sx] = (uint32_t)p[0] << 16 | p[1] << 8 | p[2];
        }
    }
    free_frames(raw, raw_bytes);
    shown = 1;
    if (current != name) {
        for (n = 0; name[n] && n < (int)sizeof(current) - 1; n++)
            current[n] = name[n];
        current[n] = '\0';
    }
    kprintf("wallpaper: %s, %ux%u, %u KB packed, unpacked and checked in %u ms\n", current, hd.w,
            hd.h, hd.packed_len / 1024, uptime_ms() - t0);
    return 0;
}

int wallpaper_refresh(void)
{
    return current[0] ? wallpaper_set(current) : -ENOENT;
}

const char *wallpaper_current(void)
{
    return shown ? current : "";
}

int wallpaper_thumb(const char *name, uint32_t *out, int w, int h)
{
    struct header hd;
    uint32_t raw, raw_bytes;
    int err = unpack(name, &hd, &raw, &raw_bytes);
    if (err)
        return err;
    /* Average a box of source pixels for each thumbnail pixel. */
    int stride = 1 + 3 * hd.w;
    for (int ty = 0; ty < h; ty++) {
        int y0 = ty * hd.h / h, y1 = (ty + 1) * hd.h / h;
        for (int tx = 0; tx < w; tx++) {
            int x0 = tx * hd.w / w, x1 = (tx + 1) * hd.w / w;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int y = y0; y < y1; y += 2)
                for (int x = x0; x < x1; x += 2, n++) {
                    const uint8_t *p = (const uint8_t *)raw + y * stride + 1 + x * 3;
                    r += p[0], g += p[1], b += p[2];
                }
            out[ty * w + tx] = n ? (r / n) << 16 | (g / n) << 8 | (b / n) : 0;
        }
    }
    free_frames(raw, raw_bytes);
    return 0;
}

int wallpaper_list(char names[][32], int max)
{
    int count = 0;
    for (int i = 0; i < ramdisk_count() && count < max; i++) {
        const char *f = ramdisk_name(i);
        int n = f ? (int)strlen(f) : 0;
        if (n < 17 || memcmp(f, "wallpapers/", 11) || strcmp(f + n - 5, ".lkxw"))
            continue;
        int len = n - 16;                   /* without "wallpapers/" and ".lkxw" */
        if (len >= 32 || (len > 6 && !memcmp(f + 11 + len - 6, "-light", 6)))
            continue;                       /* light variants go with their wallpaper */
        memcpy(names[count], f + 11, (size_t)len);
        names[count][len] = '\0';
        count++;
    }
    return count;
}

void wallpaper_draw(int x, int y, int w, int h, uint32_t dim)
{
    struct gfx_surface *scr = screen_surface();
    if (!shown) {
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
    return shown ? image.px[(image.h - 1) * image.stride + image.w - 1] : theme_get()->desktop_bg;
}

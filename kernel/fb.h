/* LiteKern X — display driver ioctl interface (drivers/vbefb.c, device "fb0").
 *
 * The framebuffer is not a byte stream, so read/write return -ENOSYS and
 * everything goes through dev_ioctl(). Pixels are 32-bit 0x00RRGGBB.
 * Rectangles are clipped to the screen; one entirely off-screen draws nothing
 * and still returns 0. Unknown commands return -ENOSYS, a NULL arg -EINVAL. */
#ifndef LKX_FB_H
#define LKX_FB_H

#include <stdint.h>

#define FB_GET_INFO  1  /* arg: struct fb_info * (out) */
#define FB_FILL_RECT 2  /* arg: const struct fb_rect * */
#define FB_BLIT      3  /* arg: const struct fb_blit * */

struct fb_info {
    uint32_t width, height;
    uint32_t pitch;         /* bytes per scanline */
    uint32_t bpp;           /* always 32 */
    uint32_t phys_addr;     /* for kernel diagnostics only; draw through ioctls */
    uint32_t wc_status;     /* enum mtrr_result (kernel/mtrr.h): MTRR_OK = write-combining */
    uint32_t wc_base, wc_len;   /* the MTRR range, when wc_status is MTRR_OK */
};

struct fb_rect {
    uint32_t x, y, w, h;
    uint32_t colour;
};

/* Copy a w x h block of pixels to (x, y). `stride` is the source's row length
 * in pixels (>= w). */
struct fb_blit {
    uint32_t x, y, w, h;
    const uint32_t *pixels;
    uint32_t stride;
};

#endif

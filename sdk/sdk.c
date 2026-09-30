/* LiteKern X — the app SDK's C side: the font kernel/gfx.c draws with (the
 * kernel's own interface, filled by SYS_FONT), the window, and logging. */
#include <stdarg.h>
#include "kernel/font.h"
#include "kernel/string.h"
#include "sdk/kern86.h"

static uint8_t glyphs[K86_FONT_BYTES];
static int loaded;

int font_init(uint32_t font_addr)
{
    (void)font_addr;
    loaded = k86_call(SYS_FONT, (uint32_t)glyphs, 0, 0, 0) == 0;
    return loaded ? 0 : -1;
}

int font_available(void)
{
    return loaded;
}

const uint8_t *font_glyph(uint8_t c)
{
    return &glyphs[c * FONT_H];
}

int k86_window_open(struct k86_window *w)
{
    int err = k86_call(SYS_WINDOW_OPEN, (uint32_t)w, 0, 0, 0);
    if (!err)
        font_init(0);
    return err;
}

int k86_log(const char *text)
{
    return k86_call(SYS_DEBUG_WRITE, (uint32_t)text, (uint32_t)strlen(text), 0, 0);
}

void k86_logf(const char *fmt, ...)
{
    char buf[256];
    int n = 0;
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p && n < (int)sizeof(buf) - 12; p++) {
        if (*p != '%') {
            buf[n++] = *p;
            continue;
        }
        p++;
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            while (s && *s && n < (int)sizeof(buf) - 12)
                buf[n++] = *s++;
        } else if (*p == 'd' || *p == 'u' || *p == 'x') {
            uint32_t v = va_arg(ap, uint32_t), base = *p == 'x' ? 16 : 10;
            char digits[12];
            int k = 0;
            if (*p == 'd' && (int32_t)v < 0) {
                buf[n++] = '-';
                v = (uint32_t)-(int32_t)v;
            }
            do
                digits[k++] = "0123456789abcdef"[v % base];
            while (v /= base);
            while (k)
                buf[n++] = digits[--k];
        } else if (*p) {
            buf[n++] = *p;
        } else {
            break;
        }
    }
    va_end(ap);
    buf[n] = '\0';
    k86_log(buf);
}

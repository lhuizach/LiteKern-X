/* LiteKern X — renderer and screen self-test (Phase 2 §1). Linked into test
 * builds only (-DLKX_SELFTEST_GFX); see tests/kernel/test-kernel.sh.
 *
 * Pixel-exact checks of kernel/gfx.c on small RAM surfaces fenced by guard
 * words (so any write outside a surface is caught), the damage list, and
 * that screen_present() copies exactly the damaged areas to the display.
 * Then timings for the EeePC, and a test pattern left on screen. Any failed
 * check panics, so on real hardware the screen turns red. */
#include "kernel/console.h"
#include "kernel/cursor.h"
#include "kernel/fb.h"
#include "kernel/font.h"
#include "kernel/gfx.h"
#include "kernel/io.h"
#include "kernel/printk.h"
#include "kernel/screen.h"
#include "kernel/timing.h"

#define W 40
#define H 30
#define GUARD 64
#define CANARY 0xdeadbeefu

static uint32_t mem[GUARD + W * H + GUARD];
static struct gfx_surface s = { mem + GUARD, W, H, W };
static int passed, failed;

static void check(int ok, const char *what)
{
    kprintf("selftest: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (ok)
        passed++;
    else
        failed++;
}

static void reset(uint32_t colour)
{
    for (int i = 0; i < GUARD; i++)
        mem[i] = mem[GUARD + W * H + i] = CANARY;
    for (int i = 0; i < W * H; i++)
        s.px[i] = colour;
}

static int guards_intact(void)
{
    for (int i = 0; i < GUARD; i++)
        if (mem[i] != CANARY || mem[GUARD + W * H + i] != CANARY)
            return 0;
    return 1;
}

static uint32_t at(int x, int y) { return s.px[y * W + x]; }

static int count(uint32_t colour)
{
    int n = 0;
    for (int i = 0; i < W * H; i++)
        n += s.px[i] == colour;
    return n;
}

static int near(uint32_t a, uint32_t b)
{
    for (int sh = 0; sh < 24; sh += 8) {
        int d = (int)(a >> sh & 0xff) - (int)(b >> sh & 0xff);
        if (d < -1 || d > 1)
            return 0;
    }
    return 1;
}

static void test_primitives(void)
{
    reset(0);
    gfx_fill_rect(&s, -5, -5, 10, 10, 0x123456);
    check(at(0, 0) == 0x123456 && at(4, 4) == 0x123456 && at(5, 5) == 0 && count(0x123456) == 25,
          "fill_rect clips at the top-left edge");
    gfx_fill_rect(&s, W - 3, H - 2, 100, 100, 0xabcdef);
    check(count(0xabcdef) == 6 && at(W - 1, H - 1) == 0xabcdef, "fill_rect clips at the bottom-right edge");
    gfx_fill_rect(&s, -100, 5, 50, 5, 0x111111);
    gfx_fill_rect(&s, W, 0, 5, 5, 0x111111);
    check(count(0x111111) == 0 && guards_intact(), "fill_rect entirely off-surface draws nothing");

    reset(0);
    gfx_rect_outline(&s, 2, 3, 6, 5, 0xff0000);
    check(at(2, 3) == 0xff0000 && at(7, 7) == 0xff0000 && at(4, 5) == 0 &&
              count(0xff0000) == 2 * 6 + 2 * 3,
          "rect_outline draws the border only");

    reset(0);
    gfx_line(&s, 0, 0, 9, 9, 0x00ff00);
    check(at(0, 0) == 0x00ff00 && at(5, 5) == 0x00ff00 && at(9, 9) == 0x00ff00 && count(0x00ff00) == 10,
          "diagonal line hits both endpoints, one pixel per step");
    reset(0);
    gfx_line(&s, 3, 1, 5, 20, 0x0000ff);
    check(at(3, 1) == 0x0000ff && at(5, 20) == 0x0000ff && count(0x0000ff) == 20,
          "steep line: one pixel per row");
    reset(0);
    gfx_line(&s, -20, -10, W + 20, H + 10, 0x777777);
    gfx_line(&s, -5, H + 5, -1, -30, 0x888888);
    check(count(0x777777) > 20 && count(0x888888) == 0 && guards_intact(),
          "lines are clipped per pixel; one entirely outside draws nothing");

    uint32_t src_px[16];
    for (int i = 0; i < 16; i++)
        src_px[i] = 0x100000 + (uint32_t)i;
    struct gfx_surface src = { src_px, 4, 4, 4 };
    reset(0);
    gfx_blit(&s, W - 2, H - 2, &src, 0, 0, 4, 4);
    check(at(W - 2, H - 2) == src_px[0] && at(W - 1, H - 1) == src_px[5] && guards_intact(),
          "blit clips at the destination edge");
    reset(0);
    gfx_blit(&s, -1, -1, &src, 0, 0, 4, 4);
    check(at(0, 0) == src_px[5] && at(2, 2) == src_px[15] && count(0) == W * H - 9,
          "blit clips at a negative destination, source kept in step");
    reset(0);
    gfx_blit(&s, 0, 0, &src, 2, 2, 10, 10);
    check(at(0, 0) == src_px[10] && at(1, 1) == src_px[15] && count(0) == W * H - 4,
          "blit clips at the source edge");

    uint32_t img[3] = { 0x00ff0000, 0xffff0000, 0x80ff0000 };
    reset(0x0000ff);
    gfx_blit_alpha(&s, 0, 0, img, 3, 1, 3);
    check(at(0, 0) == 0x0000ff && at(1, 0) == 0xff0000 && near(at(2, 0), 0x80007f),
          "blit_alpha: alpha 0 keeps, 255 replaces, 128 blends half");
    reset(0);
    gfx_blit_alpha(&s, W - 1, H - 1, img, 3, 1, 3);
    check(guards_intact(), "blit_alpha clips at the edge");

    reset(0x222222);
    gfx_char(&s, 1, 2, 'A', 0xffffff, 0x000000);
    const uint8_t *g = font_glyph('A');
    int exact = 1;
    for (int y = 0; y < FONT_H; y++)
        for (int x = 0; x < FONT_W; x++)
            exact &= at(1 + x, 2 + y) == ((g[y] & (0x80 >> x)) ? 0xffffffu : 0u);
    check(exact && at(0, 2) == 0x222222 && at(9, 2) == 0x222222, "char matches the font's glyph bit for bit");
    reset(0x222222);
    gfx_char(&s, 1, 2, 'A', 0xffffff, GFX_TRANSPARENT);
    check(count(0) == 0 && count(0xffffff) > 10, "transparent background leaves other pixels alone");
    reset(0);
    check(gfx_text(&s, W - 12, H - 8, "Hi!", 0xffffff, GFX_TRANSPARENT) == 24 &&
              gfx_text_width("Hi!") == 24 && guards_intact(),
          "text width is 8 px per character; text off the edge is clipped");
}

static void test_damage(void)
{
    struct gfx_surface *scr = screen_surface();
    screen_present();       /* start from an empty list */

    screen_damage(10, 10, 20, 20);
    screen_damage(25, 25, 20, 20);
    struct gfx_rect r = screen_damage_rect(0);
    check(screen_damage_count() == 1 && r.x == 10 && r.y == 10 && r.w == 35 && r.h == 35,
          "overlapping damage merges into one rectangle");
    screen_damage(100, 100, 5, 5);
    check(screen_damage_count() == 2, "separate damage stays separate");
    screen_damage(30, 0, 70, 10);   /* shares the first one's top edge; far from the second */
    check(screen_damage_count() == 2, "touching damage merges");
    screen_present();
    screen_damage(-50, -50, 10, 10);
    screen_damage(scr->w, 0, 10, 10);
    check(screen_damage_count() == 0, "damage entirely off-screen is dropped");
    screen_damage(-5, -5, 10, 10);
    r = screen_damage_rect(0);
    check(r.x == 0 && r.y == 0 && r.w == 5 && r.h == 5, "damage is clipped to the screen");
    screen_present();
    for (int i = 0; i < SCREEN_MAX_DAMAGE + 1; i++)
        screen_damage(i * 20, i * 20, 5, 5);
    r = screen_damage_rect(0);
    check(screen_damage_count() == 1 && r.x == 0 && r.y == 0 &&
              r.w == SCREEN_MAX_DAMAGE * 20 + 5,
          "a full damage list collapses into one bounding box");
    screen_present();
    check(screen_damage_count() == 0, "present clears the damage list");
}

static void test_present(void)
{
    struct fb_info info;
    dev_ioctl(device_find("fb0"), FB_GET_INFO, &info);
    volatile uint32_t *fbmem = (volatile uint32_t *)info.phys_addr;
    uint32_t pitch = info.pitch / 4;
    struct gfx_surface *scr = screen_surface();

    fbmem[50 * pitch + 50] = 0x010203;
    fbmem[60 * pitch + 60] = 0x010203;
    gfx_fill_rect(scr, 40, 40, 30, 30, 0x00a0b0c0);
    screen_damage(45, 45, 10, 10);
    uint32_t copied = screen_present();
    check(copied == 100 && fbmem[50 * pitch + 50] == 0x00a0b0c0 && fbmem[60 * pitch + 60] == 0x010203,
          "present copies exactly the damaged area to the display");
}

/* The overlay (what the mouse cursor uses): composited on the way to the
 * display, never written into the back buffer. */
static void test_overlay(void)
{
    struct fb_info info;
    dev_ioctl(device_find("fb0"), FB_GET_INFO, &info);
    volatile uint32_t *fbmem = (volatile uint32_t *)info.phys_addr;
    uint32_t pitch = info.pitch / 4;
    struct gfx_surface *scr = screen_surface();
#define FB(x, y) fbmem[(uint32_t)(y) * pitch + (uint32_t)(x)]
#define BACK(x, y) scr->px[(y) * scr->stride + (x)]

    static const uint32_t ov[4] = { 0xffff0000, 0x80ff0000, 0x00ff0000, 0xff00ff00 };
    gfx_fill_rect(scr, 300, 300, 20, 20, 0x0000ff);
    screen_damage(300, 300, 20, 20);
    screen_present();
    screen_set_overlay(ov, 2, 2, 305, 305);
    screen_present();
    check(FB(305, 305) == 0xff0000 && near(FB(306, 305), 0x80007f) && FB(305, 306) == 0x0000ff &&
              FB(306, 306) == 0x00ff00,
          "overlay is alpha-composited onto the display");
    check(BACK(305, 305) == 0x0000ff && BACK(306, 306) == 0x0000ff,
          "overlay never touches the back buffer");
    screen_set_overlay(ov, 2, 2, 310, 310);
    screen_present();
    check(FB(305, 305) == 0x0000ff && FB(306, 306) == 0x0000ff && FB(310, 310) == 0xff0000,
          "moving the overlay restores what was under it");
    gfx_fill_rect(scr, 309, 309, 4, 4, 0x123456);
    screen_damage(300, 300, 20, 20);    /* scene changes under the overlay */
    screen_present();
    check(FB(310, 310) == 0xff0000 && FB(309, 309) == 0x123456 && FB(312, 312) == 0x123456,
          "redrawing under the overlay keeps it on top");
    screen_set_overlay(ov, 2, 2, scr->w - 1, scr->h - 1);
    screen_present();
    check(FB(scr->w - 1, scr->h - 1) == 0xff0000, "an overlay half off the screen is clipped");
    screen_set_overlay(0, 0, 0, 0, 0);
    screen_present();
    check(FB(scr->w - 1, scr->h - 1) == BACK(scr->w - 1, scr->h - 1), "removing the overlay restores the screen");
    cursor_refresh();
#undef FB
#undef BACK
}

static uint32_t ms_x10(uint64_t ticks, uint32_t n)
{
    return tsc_to_ms(ticks * 10 / n);
}

static void benchmark(void)
{
    struct gfx_surface *scr = screen_surface();
    uint64_t t;

    t = rdtsc();
    for (int i = 0; i < 10; i++)
        gfx_fill_rect(scr, 0, 0, scr->w, scr->h, 0x1e3a5f);
    uint32_t fill = ms_x10(rdtsc() - t, 10);

    t = rdtsc();
    for (int i = 0; i < 10; i++) {
        screen_damage_all();
        screen_present();
    }
    uint32_t full = ms_x10(rdtsc() - t, 10);

    t = rdtsc();
    for (int i = 0; i < 100; i++) {
        screen_damage(100, 100, 32, 32);
        screen_present();
    }
    uint32_t small = ms_x10(rdtsc() - t, 100);

    t = rdtsc();
    for (int i = 0; i < 1000; i++)
        gfx_char(scr, (i % 100) * 8, 200 + (i / 100) * 16, (char)('A' + i % 26), 0xffffff, 0x1e3a5f);
    uint32_t glyphs = ms_x10(rdtsc() - t, 1);

    kprintf("gfx: bench %dx%d: fill back buffer %u.%u ms, present full screen %u.%u ms, "
            "present 32x32 %u.%u ms, 1000 glyphs %u.%u ms\n",
            scr->w, scr->h, fill / 10, fill % 10, full / 10, full % 10,
            small / 10, small % 10, glyphs / 10, glyphs % 10);
}

/* Left on screen for the screenshot / photo. */
static void test_pattern(void)
{
    struct gfx_surface *scr = screen_surface();
    static const uint32_t palette[] = { 0x0f1e33, 0x1e3a5f, 0x2e5584, 0x48a6e8,
                                        0xc8d0dc, 0xffffff, 0xe8a33d, 0xb83232 };
    gfx_fill_rect(scr, 0, 0, scr->w, scr->h, 0x1e3a5f);
    gfx_text(scr, 16, 16, "LiteKern X - rendering pipeline self-test", 0xffffff, GFX_TRANSPARENT);
    for (int i = 0; i < 8; i++) {
        gfx_fill_rect(scr, 16 + i * 60, 48, 52, 40, palette[i]);
        gfx_rect_outline(scr, 16 + i * 60, 48, 52, 40, 0x0f1e33);
    }
    for (int i = 0; i <= 16; i++)
        gfx_line(scr, 16, 110, 16 + i * 30, 290, i % 2 ? 0x48a6e8 : 0xc8d0dc);
    uint32_t glass[64 * 64];
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            glass[y * 64 + x] = (uint32_t)(x * 4) << 24 | 0xe8a33d;   /* alpha ramp */
    for (int i = 0; i < 3; i++)
        gfx_blit_alpha(scr, 560 + i * 40, 120 + i * 30, glass, 64, 64, 64);
    gfx_text(scr, 560, 250, "alpha blend", 0xc8d0dc, GFX_TRANSPARENT);
    for (int c = 32; c < 127; c++)
        gfx_char(scr, 16 + ((c - 32) % 48) * 10, 320 + ((c - 32) / 48) * 20, (char)c, 0xc8d0dc,
                 GFX_TRANSPARENT);
    screen_damage_all();
    screen_present();
}

void selftest_gfx_run(void)
{
    if (!screen_ready())
        panic("gfx self-test: no screen");
    /* The console draws through the screen too; while it's visible, every
     * line it prints would present (emptying the damage list) and draw over
     * the pixels under test. Hidden, it still records and goes to serial,
     * and a panic shows it again. */
    console_set_visible(0);
    test_primitives();
    test_damage();
    test_present();
    test_overlay();
    benchmark();
    test_pattern();
    kprintf("selftest: gfx %d/%d passed\n", passed, passed + failed);
    if (failed)
        panic("gfx self-test: %d checks failed", failed);
}

/* LiteKern X — the screen saver (see kernel/screensaver.h). */
#include "kernel/screensaver.h"
#include "kernel/desktop.h"
#include "kernel/driver.h"
#include "kernel/io.h"
#include "kernel/printk.h"
#include "kernel/rtc.h"
#include "kernel/screen.h"
#include "kernel/timing.h"
#include "defaults.h"

#define RIBBONS 4
#define LEN 480             /* dots per ribbon: about 960 px of ribbon */
#define SPACING 2           /* px between a ribbon's dots */
#define FADE 32             /* the last FADE dots fade out towards the tail */
#define R_MIN 5             /* a dot's core radius: R_MIN..R_MAX px, its glow 5/2 of that */
#define R_MAX 8
#define MAX_G (R_MAX * 5 / 2)
#define SPRITE (2 * MAX_G + 1)
#define MAX_NEW 48          /* dots one ribbon can grow by in one frame */
#define FRAME_HZ 32         /* rtc0's periodic interrupt: a power of two */
#define FADE_STEPS 8

/* Recordings only (make EXTRA_CFLAGS=-DANIM_SLOW=N), like the desktop's:
 * everything N times slower, so screen captures catch every step. */
#ifndef ANIM_SLOW
#define ANIM_SLOW 1
#endif

/* One dot of a ribbon: where, how thick there (the core's radius; its glow
 * reaches 5/2 times as far), and its colour. */
struct dot {
    int16_t x, y;
    uint8_t r;
    uint32_t colour;
};

struct ribbon {
    struct dot dots[LEN];   /* a ring: dot number n is dots[n % LEN] */
    uint32_t count;         /* dots made so far */
    int hx, hy;             /* the head, where the last dot went */
    uint32_t twist;         /* how far along its thickness wave the ribbon is */
    struct gfx_rect stale;  /* where its tail was or fades: redrawn on its turn */
};

/* How each ribbon's head wanders: x and y are each the sum of two slow sine
 * waves with periods (ms) that don't divide each other, so the path loops
 * and sweeps but never quite repeats. */
static const struct path {
    uint32_t px1, px2, py1, py2;        /* periods */
    uint32_t ph1, ph2, ph3, ph4;        /* starting phases, in 1/256 turns */
    uint8_t hue;                        /* first colour (see palette) */
} paths[RIBBONS] = {
    { 11300, 17900, 13700, 7100, 0, 90, 40, 200, 0 },
    { 14900, 8300, 10100, 19300, 128, 30, 170, 60, 64 },
    { 9700, 21100, 16300, 8900, 60, 220, 100, 10, 128 },
    { 18700, 12100, 7900, 15100, 200, 150, 230, 120, 192 },
};

static struct {
    int active;
    uint32_t idle_since;    /* uptime_ms() of the last input */
    uint32_t t0, last_frame;
    int cx, cy, ax1, ax2, ay1, ay2;
    uint32_t frames;
    uint64_t busy;          /* TSC ticks spent drawing frames */
    device_t *rtc;
} ss;

static struct ribbon ribbons[RIBBONS];
static uint32_t palette[256];
static uint8_t glow[256], white[256];   /* by (distance / glow radius)^2, 0..255 */
/* A dot of each radius, made once: brightness and whiteness per pixel. */
static uint8_t spr_v[R_MAX + 1][SPRITE * SPRITE], spr_w[R_MAX + 1][SPRITE * SPRITE];

/* --- fixed-point maths (the kernel has no floating point) --------------------------- */

/* A quarter of a sine wave, 0..pi/2 in 256 steps, scaled to 32767. */
static const int16_t quarter_sine[257] = {
    0, 201, 402, 603, 804, 1005, 1206, 1407, 1608, 1809, 2009, 2210,
    2410, 2611, 2811, 3012, 3212, 3412, 3612, 3811, 4011, 4210, 4410, 4609,
    4808, 5007, 5205, 5404, 5602, 5800, 5998, 6195, 6393, 6590, 6786, 6983,
    7179, 7375, 7571, 7767, 7962, 8157, 8351, 8545, 8739, 8933, 9126, 9319,
    9512, 9704, 9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605,
    11793, 11980, 12167, 12353, 12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
    14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269, 15446, 15623, 15800, 15976,
    16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680, 19841, 20000,
    20159, 20317, 20475, 20631, 20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
    22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027, 23170, 23311, 23452, 23592,
    23731, 23870, 24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674,
    26790, 26905, 27019, 27133, 27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
    28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803, 28898, 28992, 29085, 29177,
    29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050,
    31113, 31176, 31237, 31297, 31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
    31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098, 32137, 32176, 32213, 32250,
    32285, 32318, 32351, 32382, 32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717, 32728, 32737, 32745, 32752,
    32757, 32761, 32765, 32766, 32767,
};

/* sin of `angle` (a full turn is 2^32), scaled to 32767, interpolated. */
static int sine(uint32_t angle)
{
    uint32_t p = angle & 0x3fffffffu;               /* within the quarter */
    if (angle & 0x40000000u)
        p = 0x40000000u - p;                        /* the 2nd and 4th quarters run back */
    uint32_t i = p >> 22, frac = (p >> 6) & 0xffff;
    int v = i >= 256 ? quarter_sine[256]
                     : quarter_sine[i] + (int)(((quarter_sine[i + 1] - quarter_sine[i]) * (int)frac) >> 16);
    return angle & 0x80000000u ? -v : v;
}

static uint32_t isqrt(uint32_t n)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* --- looks ------------------------------------------------------------------------------ */

/* The colours the ribbons slide through, round and round. */
static const uint32_t stops[] = { 0x2f7bff, 0x00d8ff, 0x3dff8a, 0xffe14d, 0xff8a2a,
                                  0xff3da6, 0xb04dff };
#define NSTOPS (int)(sizeof(stops) / sizeof(stops[0]))

static uint32_t mix(uint32_t a, uint32_t b, int t)      /* a..b, t 0..256 */
{
    uint32_t r = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (int)(a >> sh & 0xff), cb = (int)(b >> sh & 0xff);
        r |= (uint32_t)(ca + (cb - ca) * t / 256) << sh;
    }
    return r;
}

static void make_tables(void)
{
    for (int i = 0; i < 256; i++) {
        int pos = i * NSTOPS;               /* 1/256ths of the way round the stops */
        int s = pos >> 8;
        palette[i] = mix(stops[s], stops[(s + 1) % NSTOPS], pos & 0xff);
    }
    /* A dot: a solid core out to 1/3 of its radius (u < 28), white-hot in
     * the middle, with a soft edge, then a glow that dies away to nothing. */
    for (int u = 0; u < 256; u++) {
        int g = 255 - u;                    /* 255 at the centre, 0 at the edge */
        int halo = 150 * g / 255 * g / 255 * g / 255;
        int core = u < 22 ? 255 : u < 34 ? 255 - (u - 22) * (255 - halo) / 12 : halo;
        glow[u] = (uint8_t)core;
        white[u] = (uint8_t)(u < 10 ? 110 : u < 30 ? 110 - (u - 10) * 110 / 20 : 0);
    }
    for (int r = R_MIN; r <= R_MAX; r++) {
        int g = r * 5 / 2;
        uint32_t inv = (256u << 16) / (uint32_t)(g * g);
        for (int y = -g; y <= g; y++)
            for (int x = -g; x <= g; x++) {
                uint32_t u = ((uint32_t)(x * x + y * y) * inv) >> 16;
                int i = (y + g) * SPRITE + (x + g);
                spr_v[r][i] = u > 255 ? 0 : glow[u];
                spr_w[r][i] = u > 255 ? 0 : white[u];
            }
    }
}

/* --- drawing ------------------------------------------------------------------------------ */

static struct gfx_rect dot_rect(const struct dot *d)
{
    int g = d->r * 5 / 2;
    return (struct gfx_rect){ d->x - g, d->y - g, 2 * g + 1, 2 * g + 1 };
}

/* Light up dot d inside clip at `level` (0..256): each colour channel
 * becomes the larger of what's there and the dot's, so overlapping dots
 * (of one ribbon or several) look the same whatever order they're drawn
 * in, and a ribbon is one smooth tube, not a string of beads. */
static void draw_dot(struct gfx_surface *s, struct gfx_rect clip, const struct dot *d, int level)
{
    struct gfx_rect r = gfx_rect_intersect(dot_rect(d), clip);
    if (gfx_rect_empty(r))
        return;
    int g = d->r * 5 / 2;
    const uint8_t *sv = spr_v[d->r], *sw = spr_w[d->r];
    uint32_t cr = d->colour >> 16 & 0xff, cg = d->colour >> 8 & 0xff, cb = d->colour & 0xff;
    uint32_t wr = 255 - cr, wg = 255 - cg, wb = 255 - cb;     /* how far each is from white */
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t *row = s->px + y * s->stride;
        int si = (y - d->y + g) * SPRITE + (r.x - d->x + g);
        for (int x = r.x; x < r.x + r.w; x++, si++) {
            uint32_t v = sv[si];
            if (!v)
                continue;
            v = v * (uint32_t)level >> 8;
            uint32_t w = sw[si];
            uint32_t pr = (cr + (wr * w >> 8)) * v >> 8;
            uint32_t pg = (cg + (wg * w >> 8)) * v >> 8;
            uint32_t pb = (cb + (wb * w >> 8)) * v >> 8;
            uint32_t o = row[x];
            uint32_t orr = o >> 16 & 0xff, og = o >> 8 & 0xff, ob = o & 0xff;
            row[x] = (pr > orr ? pr : orr) << 16 | (pg > og ? pg : og) << 8 | (pb > ob ? pb : ob);
        }
    }
}

/* How bright dot number n of ribbon b is: full, except the last FADE
 * before the tail, which fade out. */
static int level_of(const struct ribbon *b, uint32_t n)
{
    uint32_t tail = b->count > LEN ? b->count - LEN : 0;
    uint32_t j = n - tail;
    return j < FADE ? (int)((j + 1) * 256 / (FADE + 1)) : 256;
}

/* Redraw r from nothing: black, then every dot of every ribbon that
 * reaches into it. */
static void redraw(struct gfx_rect r)
{
    struct gfx_surface *s = screen_surface();
    r = gfx_rect_intersect(r, (struct gfx_rect){ 0, 0, s->w, s->h });
    if (gfx_rect_empty(r))
        return;
    gfx_fill_rect(s, r.x, r.y, r.w, r.h, 0);
    for (int i = 0; i < RIBBONS; i++) {
        const struct ribbon *b = &ribbons[i];
        uint32_t tail = b->count > LEN ? b->count - LEN : 0;
        for (uint32_t n = tail; n < b->count; n++) {
            const struct dot *d = &b->dots[n % LEN];
            int g = d->r * 5 / 2;
            if (d->x + g < r.x || d->x - g >= r.x + r.w || d->y + g < r.y || d->y - g >= r.y + r.h)
                continue;
            draw_dot(s, r, d, level_of(b, n));
        }
    }
    screen_damage(r.x, r.y, r.w, r.h);
    screen_present();
}

/* Where ribbon i's head is at t ms. */
static void head_at(int i, uint32_t t, int *x, int *y)
{
    const struct path *p = &paths[i];
#define WAVE(period, phase) sine((uint32_t)(((uint64_t)t << 32) / (period)) + ((uint32_t)(phase) << 24))
    *x = ss.cx + (ss.ax1 * WAVE(p->px1, p->ph1) + ss.ax2 * WAVE(p->px2, p->ph2)) / 32767;
    *y = ss.cy + (ss.ay1 * WAVE(p->py1, p->ph3) + ss.ay2 * WAVE(p->py2, p->ph4)) / 32767;
#undef WAVE
}

/* Grow ribbon i to where its head is at t: new dots every SPACING px
 * along the way, drawn straight on. Where its tail was and where it fades
 * now is redrawn on the ribbon's turn (`redraw_tail`): every other frame,
 * which halves that cost and still looks smooth. */
static void grow(int i, uint32_t t, int redraw_tail)
{
    struct ribbon *b = &ribbons[i];
    int nx, ny;
    head_at(i, t, &nx, &ny);
    int dx = nx - b->hx, dy = ny - b->hy;
    int steps = (int)isqrt((uint32_t)(dx * dx + dy * dy)) / SPACING;
    if (steps < 1)
        return;
    if (steps > MAX_NEW)
        steps = MAX_NEW;

    /* The tail's area, before its dots are reused for the head. */
    uint32_t old_tail = b->count > LEN ? b->count - LEN : 0;
    uint32_t count = b->count + (uint32_t)steps;
    uint32_t new_tail = count > LEN ? count - LEN : 0;
    struct gfx_rect gone = b->stale;
    if (new_tail != old_tail)
        for (uint32_t n = old_tail; n < new_tail + FADE && n < b->count; n++)
            gone = gfx_rect_empty(gone) ? dot_rect(&b->dots[n % LEN])
                                        : gfx_rect_union(gone, dot_rect(&b->dots[n % LEN]));

    struct gfx_surface *s = screen_surface();
    struct gfx_rect all = { 0, 0, s->w, s->h }, added = { 0, 0, 0, 0 };
    uint32_t hue = (uint32_t)paths[i].hue + t / 90;     /* round the palette in ~23 s */
    for (int k = 1; k <= steps; k++) {
        struct dot *d = &b->dots[b->count % LEN];
        b->twist += 3;
        int tw = sine(b->twist << 24);                  /* it thickens and thins: a twist */
        d->x = (int16_t)(b->hx + dx * k / steps);
        d->y = (int16_t)(b->hy + dy * k / steps);
        d->r = (uint8_t)(R_MIN + (tw + 32767) * (R_MAX - R_MIN) / 65535);
        d->colour = palette[hue & 0xff];
        b->count++;
        draw_dot(s, all, d, level_of(b, b->count - 1));
        added = gfx_rect_empty(added) ? dot_rect(d) : gfx_rect_union(added, dot_rect(d));
    }
    b->hx = nx;
    b->hy = ny;
    added = gfx_rect_intersect(added, all);
    if (!gfx_rect_empty(added)) {
        screen_damage(added.x, added.y, added.w, added.h);
        screen_present();
    }
    b->stale = gone;
    if (redraw_tail && !gfx_rect_empty(gone)) {
        redraw(gone);
        b->stale = (struct gfx_rect){ 0, 0, 0, 0 };
    }
}

static void frame(void)
{
    uint64_t start = rdtsc();
    uint32_t t = (uptime_ms() - ss.t0) / ANIM_SLOW;
    for (int i = 0; i < RIBBONS; i++)
        grow(i, t, (ss.frames + (uint32_t)i) % 2 == 0);
    ss.frames++;
    ss.busy += rdtsc() - start;
}

/* --- starting and stopping ------------------------------------------------------------------ */

static void set_rate(uint32_t hz)
{
    if (ss.rtc)
        dev_ioctl(ss.rtc, RTC_SET_RATE, &hz);
}

void screensaver_start(void)
{
    if (ss.active || !screen_ready() || !desktop_started())
        return;
    struct gfx_surface *s = screen_surface();
    ss.rtc = device_find("rtc0");
    if (ss.rtc && ss.rtc->state != DEVICE_BOUND)
        ss.rtc = 0;
    make_tables();
    ss.cx = s->w / 2;
    ss.cy = s->h / 2;
    ss.ax1 = s->w * 30 / 100;
    ss.ax2 = s->w * 15 / 100;
    ss.ay1 = s->h * 28 / 100;
    ss.ay2 = s->h * 14 / 100;

    kprintf("screensaver: on (idle %u s)\n", (uptime_ms() - ss.idle_since) / 1000);
    ss.active = 1;
    desktop_cover(1);                       /* the desktop stops drawing */
    screen_set_overlay(0, 0, 0, 0, 0);      /* no pointer */

    /* The desktop fades to black; the ribbons draw themselves in. */
    for (int i = 1; i <= FADE_STEPS; i++) {
        uint32_t f0 = uptime_ms();
        gfx_darken(s, 0, 0, s->w, s->h, 60);
        screen_damage_all();
        screen_present();
        while (uptime_ms() - f0 < 30 * ANIM_SLOW)
            __asm__ volatile("pause");
    }
    gfx_fill_rect(s, 0, 0, s->w, s->h, 0);
    screen_damage_all();
    screen_present();
    ss.t0 = ss.last_frame = uptime_ms();
    for (int i = 0; i < RIBBONS; i++) {
        ribbons[i].count = 0;
        ribbons[i].twist = (uint32_t)i * 64;
        ribbons[i].stale = (struct gfx_rect){ 0, 0, 0, 0 };
        head_at(i, 0, &ribbons[i].hx, &ribbons[i].hy);
    }
    ss.frames = 0;
    ss.busy = 0;
    set_rate(FRAME_HZ);
}

static void stop(void)
{
    set_rate(0);
    ss.active = 0;
    uint32_t ms = uptime_ms() - ss.t0, busy = tsc_to_ms(ss.busy);
    uint32_t permille = ms ? busy * 1000 / ms : 0;
    kprintf("screensaver: off after %u s: %u frames (%u fps), drawing took %u.%u%% of the time\n",
            ms / 1000, ss.frames, ms ? ss.frames * 1000 / ms : 0, permille / 10, permille % 10);
    desktop_cover(0);                       /* redraws everything, pointer included */
}

int screensaver_active(void)
{
    return ss.active;
}

int screensaver_input(void)
{
    ss.idle_since = uptime_ms();
    if (!ss.active)
        return 0;
    stop();
    return 1;
}

static uint32_t idle_s = DEFAULT_SCREENSAVER_S;     /* Settings can change it (0: never) */

uint32_t screensaver_timeout(void)
{
    return idle_s;
}

void screensaver_set_timeout(uint32_t seconds)
{
    idle_s = seconds;
    ss.idle_since = uptime_ms();        /* counting starts again from now */
}

void screensaver_poll(void)
{
    if (!ss.active) {
        if (idle_s && desktop_started() && uptime_ms() - ss.idle_since >= idle_s * 1000)
            screensaver_start();
        return;
    }
    uint32_t now = uptime_ms();
    if (now - ss.last_frame < 1000 / FRAME_HZ - 3)
        return;                 /* woken early (the clock's 1 Hz tick): not due yet */
    ss.last_frame = now;
    frame();
}

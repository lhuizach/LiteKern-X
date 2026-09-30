#include "kernel/inflate.h"
#include "kernel/errno.h"

/* A canonical Huffman code as DEFLATE defines it: how many codes of each
 * length, and the symbols in code order. Decoding walks the lengths (the
 * "puff" approach): simple, small, and fast enough for boot-time assets. */
struct huffman {
    uint16_t count[16];
    uint16_t symbol[288];
};

struct state {
    const uint8_t *in;
    size_t in_len, in_pos;
    uint32_t bits;          /* bit buffer, LSB first */
    int nbits;
    uint8_t *out;
    size_t out_len, out_pos;
    int error;
};

static int need(struct state *s, int n)
{
    while (s->nbits < n) {
        if (s->in_pos == s->in_len) {
            s->error = -EINVAL;             /* ran out of input */
            return 0;
        }
        s->bits |= (uint32_t)s->in[s->in_pos++] << s->nbits;
        s->nbits += 8;
    }
    return 1;
}

static uint32_t bits(struct state *s, int n)
{
    if (!n || !need(s, n))
        return 0;
    uint32_t v = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nbits -= n;
    return v;
}

/* Build a code from symbol lengths. 0, or -EINVAL if the lengths are
 * over-subscribed (an incomplete code is allowed, as zlib allows it). */
static int build(struct huffman *h, const uint8_t *lengths, int n)
{
    uint16_t offs[16];
    for (int i = 0; i < 16; i++)
        h->count[i] = 0;
    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0)
            return -EINVAL;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (lengths[i])
            h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    return 0;
}

static int decode(struct state *s, const struct huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)bits(s, 1);
        if (s->error)
            return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->error = -EINVAL;                     /* no such code */
    return -1;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(struct state *s, const struct huffman *lit, const struct huffman *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (s->error)
            return s->error;
        if (sym < 256) {
            if (s->out_pos == s->out_len)
                return -ENOSPC;
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;                       /* end of block */
        } else {
            sym -= 257;
            if (sym >= 29)
                return -EINVAL;
            size_t len = len_base[sym] + bits(s, len_extra[sym]);
            int d = decode(s, dist);
            if (s->error)
                return s->error;
            if (d >= 30)
                return -EINVAL;
            size_t back = dist_base[d] + bits(s, dist_extra[d]);
            if (s->error)
                return s->error;
            if (back > s->out_pos)
                return -EINVAL;             /* reaches before the start */
            if (len > s->out_len - s->out_pos)
                return -ENOSPC;
            for (; len; len--, s->out_pos++)
                s->out[s->out_pos] = s->out[s->out_pos - back];
        }
    }
}

static int stored(struct state *s)
{
    s->bits = 0;                            /* skip to a byte boundary */
    s->nbits = 0;
    if (s->in_len - s->in_pos < 4)
        return -EINVAL;
    unsigned len = s->in[s->in_pos] | s->in[s->in_pos + 1] << 8;
    unsigned nlen = s->in[s->in_pos + 2] | s->in[s->in_pos + 3] << 8;
    s->in_pos += 4;
    if (len != (~nlen & 0xffff) || len > s->in_len - s->in_pos)
        return -EINVAL;
    if (len > s->out_len - s->out_pos)
        return -ENOSPC;
    for (; len; len--)
        s->out[s->out_pos++] = s->in[s->in_pos++];
    return 0;
}

static int fixed(struct state *s)
{
    static struct huffman lit, dist;
    static int built;
    if (!built) {
        uint8_t l[288];
        for (int i = 0; i < 288; i++)
            l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        build(&lit, l, 288);
        for (int i = 0; i < 30; i++)
            l[i] = 5;
        build(&dist, l, 30);
        built = 1;
    }
    return codes(s, &lit, &dist);
}

static int dynamic(struct state *s)
{
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    struct huffman lencode, lit, dist;
    uint8_t lengths[320];
    int nlen = (int)bits(s, 5) + 257, ndist = (int)bits(s, 5) + 1, ncode = (int)bits(s, 4) + 4;
    if (s->error)
        return s->error;
    if (nlen > 286 || ndist > 30)
        return -EINVAL;
    for (int i = 0; i < 19; i++)
        lengths[order[i]] = i < ncode ? (uint8_t)bits(s, 3) : 0;
    if (s->error || build(&lencode, lengths, 19))
        return -EINVAL;

    for (int i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lencode);
        if (s->error)
            return s->error;
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        uint8_t len = 0;
        int repeat;
        if (sym == 16) {
            if (i == 0)
                return -EINVAL;             /* nothing to repeat */
            len = lengths[i - 1];
            repeat = 3 + (int)bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + (int)bits(s, 3);
        } else {
            repeat = 11 + (int)bits(s, 7);
        }
        if (s->error || i + repeat > nlen + ndist)
            return -EINVAL;
        while (repeat--)
            lengths[i++] = len;
    }
    if (lengths[256] == 0)
        return -EINVAL;                     /* no end-of-block code */
    if (build(&lit, lengths, nlen) || build(&dist, lengths + nlen, ndist))
        return -EINVAL;
    return codes(s, &lit, &dist);
}

int inflate_raw(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    struct state s = { .in = in, .in_len = in_len, .out = out, .out_len = out_len };
    int last, err;
    do {
        last = (int)bits(&s, 1);
        int type = (int)bits(&s, 2);
        if (s.error)
            return s.error;
        switch (type) {
        case 0:  err = stored(&s); break;
        case 1:  err = fixed(&s); break;
        case 2:  err = dynamic(&s); break;
        default: err = -EINVAL; break;
        }
        if (err)
            return err;
    } while (!last);
    return (int)s.out_pos;
}

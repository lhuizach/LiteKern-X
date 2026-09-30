/* LiteKern X — DEFLATE decompression (RFC 1951), for packed assets such as
 * the wallpaper (tools/wallpaper-pack.py).
 *
 * Raw deflate (no zlib or gzip wrapper). Every read and write is bounds
 * checked: a corrupt stream returns an error, it never overruns. */
#ifndef LKX_INFLATE_H
#define LKX_INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* Decompress in[0..in_len) into out[0..out_len). Returns the number of bytes
 * written, or -EINVAL for a corrupt stream, -ENOSPC if out is too small. */
int inflate_raw(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);

#endif

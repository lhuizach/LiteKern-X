"""LiteKern X - minimal PNG reader/writer for the build tools (standard library only).

read_rgba(path) -> (width, height, rows) where rows[y][x] is an (r, g, b, a) tuple.
Supports 8-bit RGBA (colour type 6) and RGB (type 2), all five row filters, no
interlacing: what Piskel, Aseprite, GIMP and tools/cursor-grid2png.py write.
write_rgba(path, width, height, rows) writes 8-bit RGBA.
"""
import struct
import zlib


class PngError(Exception):
    pass


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_rgba(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise PngError(f"{path}: not a PNG")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + n
    if not ihdr:
        raise PngError(f"{path}: no IHDR")
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (0, 2, 6) or interlace:
        raise PngError(f"{path}: need 8-bit grey/RGB/RGBA, not interlaced "
                       f"(got depth {depth}, colour type {ctype}, interlace {interlace})")
    bpp = {0: 1, 2: 3, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    rows = []
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xff
            elif f == 2:
                line[i] = (line[i] + b) & 0xff
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xff
            elif f == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 0xff
            elif f != 0:
                raise PngError(f"{path}: bad filter {f} on row {y}")
        if bpp == 1:                    # grey: (v, v, v, 255)
            rows.append([(v, v, v, 255) for v in line])
        else:
            rows.append([tuple(line[x * bpp:x * bpp + bpp]) + ((255,) if bpp == 3 else ())
                         for x in range(w)])
        prev = line
    return w, h, rows


def write_rgba(path, w, h, rows):
    raw = b"".join(b"\0" + b"".join(bytes(p) for p in r) for r in rows)
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

#!/usr/bin/env python3
"""LiteKern X - driving a running QEMU through its monitor socket, for the tests.

  qemu_monitor.py shot SOCKET OUT.png        save a screendump as PNG
  qemu_monitor.py check FILE.png CHECK...    print one "screen: ..." line per check
  qemu_monitor.py keys SOCKET KEY...         type keys (QEMU sendkey names:
                                             a, shift-a, ret, up, ctrl-c, ...)

Checks (colours are RRGGBB, as the kernel writes 0x00RRGGBB):
  corner=RRGGBB                 bottom-right pixel has this colour
  has=RRGGBB@x0,y0,x1,y1        at least one pixel of this colour in the box
"""
import os, socket, struct, sys, time, zlib


def shot(sock_path, out):
    ppm = f"/tmp/lkx-screendump-{os.getpid()}.ppm"   # QEMU can't write to /mnt/c reliably
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    s.sendall(b"screendump " + ppm.encode() + b"\n")
    for _ in range(50):             # wait for QEMU to finish writing it
        time.sleep(0.1)
        if os.path.exists(ppm) and os.path.getsize(ppm) > 64:
            break
    s.close()
    time.sleep(0.2)
    w, h, px = read_ppm(ppm)
    rows = b"".join(b"\0" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    os.remove(ppm)


def keys(sock_path, names):
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    for name in names:
        s.sendall(b"sendkey " + name.encode() + b"\n")
        time.sleep(0.15)            # let the guest take each key before the next
    time.sleep(0.2)
    s.close()


def read_ppm(path):
    d = open(path, "rb").read()
    magic, dims, maxval, px = d.split(b"\n", 3)
    w, h = map(int, dims.split())
    return w, h, px


def read_png(path):
    d = open(path, "rb").read()
    w, h = struct.unpack(">II", d[16:24])
    idat, i = b"", 8
    while i < len(d):
        n, t = struct.unpack(">I4s", d[i:i + 8])
        if t == b"IDAT":
            idat += d[i + 8:i + 8 + n]
        i += 12 + n
    raw = zlib.decompress(idat)     # written by shot(): filter 0 on every row
    px = b"".join(raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h))
    return w, h, px


def check(path, checks):
    w, h, px = read_png(path)
    at = lambda x, y: px[(y * w + x) * 3:(y * w + x) * 3 + 3].hex()
    print(f"screen: {w}x{h}")
    for c in checks:
        kind, arg = c.split("=", 1)
        if kind == "corner":
            print(f"screen: corner {at(w - 1, h - 1)}")
        elif kind == "has":
            colour, box = arg.split("@")
            x0, y0, x1, y1 = map(int, box.split(","))
            found = any(at(x, y) == colour for y in range(y0, min(y1, h)) for x in range(x0, min(x1, w)))
            print(f"screen: has {colour} in {box}: {'yes' if found else 'no'}")


if __name__ == "__main__":
    if sys.argv[1] == "shot":
        shot(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "keys":
        keys(sys.argv[2], sys.argv[3:])
    else:
        check(sys.argv[2], sys.argv[3:])

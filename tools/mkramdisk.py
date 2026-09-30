#!/usr/bin/env python3
"""LiteKern X - build step: pack files into the kernel's ramdisk.

Usage: python3 tools/mkramdisk.py OUT.bin NAME=FILE [NAME=FILE ...]

The ramdisk (kernel/ramdisk.h) is where apps come from (Phase 2 §5): each
app's kerns.json and .lkx, e.g. apps/files/kerns.json. Format, little-endian:
  "LKXR", u32 count, then count entries of { char name[56]; u32 offset;
  u32 size } (offsets from the start of the ramdisk), then the data, each
  file 4-byte aligned.
"""
import struct
import sys

NAME_MAX = 56


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: mkramdisk.py OUT.bin NAME=FILE ...")
    out, pairs = sys.argv[1], sys.argv[2:]
    files = []
    for pair in pairs:
        name, _, path = pair.partition("=")
        if not name or not path or len(name.encode()) >= NAME_MAX:
            sys.exit(f"mkramdisk: bad entry {pair!r}")
        files.append((name, open(path, "rb").read()))
    offset = 8 + len(files) * (NAME_MAX + 8)
    table, data = b"", b""
    for name, blob in files:
        pad = (-offset) % 4
        data += b"\0" * pad
        offset += pad
        table += name.encode().ljust(NAME_MAX, b"\0") + struct.pack("<II", offset, len(blob))
        data += blob
        offset += len(blob)
    with open(out, "wb") as f:
        f.write(b"LKXR" + struct.pack("<I", len(files)) + table + data)
    print(f"mkramdisk: {len(files)} file(s), {offset // 1024} KB -> {out}")


if __name__ == "__main__":
    main()

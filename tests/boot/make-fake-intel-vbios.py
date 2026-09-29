#!/usr/bin/env python3
"""LiteKern X - build a QEMU video BIOS that carries an Intel-style mode table.

Takes QEMU's SeaVGABIOS and appends one 512-byte block holding a copy of the
GMA 950 mode table and resolution records as read from the EeePC 1000HE
(docs/HARDWARE-TEST.md §4C, 2026-09-29), then fixes the ROM size byte and
checksum. SeaVGABIOS ignores the extra block; stage 2's patch_intel_vbios
finds it, so the unlock/patch/read-back/relock path can be tested in QEMU's
q35 machine (same PAM registers as the 945).

Usage: make-fake-intel-vbios.py IN.bin OUT.bin
"""
import sys

# First 8 bytes of each type-1 resolution record, as dumped on the EeePC.
RECORDS = {
    "640x480":   bytes.fromhex("d6 09 80 90 20 e0 1d 10"),
    "800x600":   bytes.fromhex("a0 0f 20 00 31 58 1c 20"),
    "1024x768":  bytes.fromhex("64 19 00 40 41 00 26 30"),
    "1920x1440": bytes.fromhex("68 5b 80 a8 72 a0 3c 50"),
}
# (internal mode, bpp, record) in the order the real table starts, plus the
# 32 bpp 1920x1440 mode the patch targets.
TABLE = [(0x30, 8, "640x480"), (0x32, 8, "800x600"), (0x34, 8, "1024x768"),
         (0x3c, 8, "1920x1440"), (0x5c, 32, "1920x1440")]
RECORD_SIZE = 26


def main(src, dst):
    rom = bytearray(open(src, "rb").read())
    assert rom[0:2] == b"\x55\xaa", "not an option ROM"
    size = rom[2] * 512
    rom = rom[:size]
    base = size                         # the appended block starts here

    block = bytearray(512)
    table_len = len(TABLE) * 5 + 1
    names = list(RECORDS)
    offsets = {n: base + table_len + i * RECORD_SIZE for i, n in enumerate(names)}
    for i, (mode, bpp, rec) in enumerate(TABLE):
        off = offsets[rec]
        block[i * 5:i * 5 + 5] = bytes([mode, bpp, off & 0xff, off >> 8, 0])
    block[len(TABLE) * 5] = 0xff        # end of table
    for n in names:
        o = offsets[n] - base
        block[o:o + 8] = RECORDS[n]

    rom += block
    rom[2] = len(rom) // 512
    rom[-1] = (-sum(rom[:-1])) & 0xff   # option ROM checksum: all bytes sum to 0
    assert sum(rom) % 256 == 0
    open(dst, "wb").write(rom)
    print(f"{dst}: {len(rom)} bytes, table at +0x{base:04x}, "
          f"1920x1440 record at +0x{offsets['1920x1440']:04x}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])

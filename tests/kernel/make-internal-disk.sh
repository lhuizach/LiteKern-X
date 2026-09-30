#!/usr/bin/env bash
# LiteKern X — a stand-in for the EeePC's internal disk, for the ATA tests:
# an MBR with a FAT32 partition (files on it) and an "NTFS" one (type 0x07,
# just zeros: it must be listed as unsupported and never touched).
# Usage: bash tests/kernel/make-internal-disk.sh OUT.img
set -euo pipefail
out=$1
tmp=$(dirname "$out")/internal-fat.img
rm -f "$tmp" "$out"
mkfs.fat -C -F 32 -s 1 -h 2048 -n WINDOWS -i 4c4b5849 --invariant "$tmp" $((64 * 1024)) >/dev/null
printf 'Hello from the internal disk.\r\n' > "$(dirname "$out")/Internal notes.txt"
MTOOLS_SKIP_CHECK=1 mcopy -i "$tmp" "$(dirname "$out")/Internal notes.txt" ::/
MTOOLS_SKIP_CHECK=1 mmd -i "$tmp" ::/Programs
python3 - "$out" <<'PY'
import struct, sys
mbr = bytearray(512)
def part(i, boot, ptype, lba, size):
    struct.pack_into("<B3sB3sII", mbr, 446 + 16 * i, boot, b"\xfe\xff\xff", ptype, b"\xfe\xff\xff", lba, size)
part(0, 0x80, 0x0c, 2048, 131072)              # FAT32, 64 MiB
part(1, 0x00, 0x07, 2048 + 131072, 16384)      # "NTFS", 8 MiB
mbr[440:444] = b"WINX"
mbr[510:512] = b"\x55\xaa"
open(sys.argv[1], "wb").write(bytes(mbr))
PY
truncate -s $((2048 * 512)) "$out"
cat "$tmp" >> "$out"
truncate -s $(((2048 + 131072 + 16384) * 512)) "$out"
rm -f "$tmp"

#!/usr/bin/env bash
# LiteKern X — FAT32 driver test against Linux's own FAT tools (Phase 2 §5a).
#
# Builds kernel/fat32.c for Linux (with AddressSanitizer), runs it on a copy
# of the FAT32 partition the build makes, then:
#   - fsck.fat must find nothing wrong with what it wrote
#   - mtools must list exactly the files and folders it says are there
#
# Usage: bash tests/fat/test-fat.sh   (or: make test-fat)
set -uo pipefail
cd "$(dirname "$0")/../.."

make -s build/fat.img >/dev/null || exit 1
mkdir -p build/test-fat
gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -I. \
    kernel/fat32.c tests/fat/fat_host.c -o build/test-fat/fat_host || exit 1

img=build/test-fat/fat.img
cp build/fat.img "$img"
out=$(build/test-fat/fat_host "$img" 2>&1)
status=$?
grep -v '^tree: ' <<<"$out"

failures=0
[ $status -eq 0 ] || failures=$((failures + 1))

# fsck.fat: -n never changes anything; exit 0 means no errors.
fsck=$(fsck.fat -n -v "$img" 2>&1)
if [ $? -eq 0 ] && ! grep -qiE "wrong|invalid|bad |lost|mismatch|differ" <<<"$fsck"; then
    echo "PASS  fsck.fat finds nothing wrong"
else
    echo "FAIL  fsck.fat:"
    sed 's/^/      /' <<<"$fsck"
    failures=$((failures + 1))
fi

# mtools' view of the tree must match ours.
ours=$(grep '^tree: ' <<<"$out" | sed 's/^tree: //' | sort)
# `mdir -/ -b` lists every path, folders ending in "/".
theirs=$(MTOOLS_SKIP_CHECK=1 mdir -/ -b -i "$img" ::/ 2>/dev/null | sed 's|^::||' | sort)
if [ "$ours" = "$theirs" ]; then
    echo "PASS  mtools lists the same $(wc -l <<<"$ours") files and folders"
else
    echo "FAIL  mtools and our driver disagree:"
    diff <(echo "$ours") <(echo "$theirs") | sed 's/^/      /'
    failures=$((failures + 1))
fi

echo
if [ "$failures" -eq 0 ]; then echo "fat: all tests passed"; else echo "fat: $failures failed"; exit 1; fi

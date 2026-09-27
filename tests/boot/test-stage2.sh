#!/usr/bin/env bash
# LiteKern X — stage 2 tests. Boots the real image (stage 1 + stage 2 + the
# kernel stub) plus deliberately broken variants in the headless VM.
#
# Usage: bash tests/boot/test-stage2.sh   (or: make test-boot)
set -uo pipefail
cd "$(dirname "$0")/../.."

out_dir=build/test-boot
mkdir -p "$out_dir"
failures=0

# --- build ---------------------------------------------------------------------
make -s build/litekernx.img >/dev/null || exit 1
gcc -m32 -fsyntax-only -x c boot/bootinfo.h || { echo "FAIL  boot/bootinfo.h layout asserts"; exit 1; }
echo "PASS  boot/bootinfo.h matches the assembly layout"

img=build/litekernx.img
s2=$(( $(stat -c %s build/stage2.bin) / 512 ))
kernel_off=$(( (1 + s2) * 512 ))
echo "info  stage 2: $s2 sectors; kernel at LBA $((1 + s2)), $(( $(stat -c %s build/kernel.bin) / 512 )) sectors"

# poke FILE OFFSET HEXBYTES -- overwrite bytes in place
poke() { printf "$(sed 's/\(..\)/\\x\1/g' <<<"$3")" | dd of="$1" bs=1 seek="$2" conv=notrunc status=none; }

cp "$img" "$out_dir/kernel-bad-magic.img"
poke "$out_dir/kernel-bad-magic.img" "$kernel_off" 00

cp "$img" "$out_dir/kernel-too-big.img"           # file_size = 2 MiB, mem_end = 4 MiB (otherwise consistent)
poke "$out_dir/kernel-too-big.img" $((kernel_off + 16)) 0000200000004000

cp "$img" "$out_dir/kernel-entry-outside.img"     # entry = 0x00200000
poke "$out_dir/kernel-entry-outside.img" $((kernel_off + 12)) 00002000

cp "$img" "$out_dir/kernel-truncated.img"         # header readable, body missing
truncate -s $(( kernel_off + 512 )) "$out_dir/kernel-truncated.img"

# --- cases ---------------------------------------------------------------------
# run_case NAME IMAGE TIMEOUT WANT_STATUS WANT_REGEX [-- EXTRA QEMU ARGS]
#   WANT_STATUS 124 = still running (halted) when the timeout hit
run_case() {
    local name=$1 img=$2 secs=$3 want_rc=$4 want_re=$5 out rc
    shift 5
    out=$(timeout "$secs" bash vm/qemu.sh --headless --image "$img" "$@" 2>&1 | tr -d '\r')
    rc=${PIPESTATUS[0]}
    if [ "$rc" -eq "$want_rc" ] && grep -Eq "$want_re" <<<"$out"; then
        echo "PASS  $name"
        grep -E "^(kernel-stub|LKX)" <<<"$out" | sed 's/^/      /'
    else
        echo "FAIL  $name (status $rc, want $want_rc; output must match: $want_re)"
        sed 's/^/      /' <<<"$out"
        failures=$((failures + 1))
    fi
}

run_case "boots the kernel with a valid boot_info" "$img" 20 33 \
    '^kernel-stub: OK usable=[0-9]+MiB mmap=[0-9]+ fb=[0-9]+x[0-9]+x32 '
run_case "rejects a kernel without LKXK magic" "$out_dir/kernel-bad-magic.img" 5 124 \
    '^LKX stage2: bad kernel header$'
run_case "rejects a kernel over the 448 KiB load limit" "$out_dir/kernel-too-big.img" 5 124 \
    '^LKX stage2: kernel too big$'
run_case "rejects an entry point outside the image" "$out_dir/kernel-entry-outside.img" 5 124 \
    '^LKX stage2: bad kernel header$'
run_case "reports a kernel read error" "$out_dir/kernel-truncated.img" 5 124 \
    '^LKX stage2: kernel read error$'
run_case "fails loudly without a VBE BIOS" "$img" 5 124 \
    '^LKX stage2: no usable VBE mode' -- -vga none

echo
if [ "$failures" -eq 0 ]; then echo "stage 2: all tests passed"; else echo "stage 2: $failures failed"; exit 1; fi

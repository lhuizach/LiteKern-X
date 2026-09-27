#!/usr/bin/env bash
# LiteKern X — stage 1 tests. Boots the headless VM against a good image and
# deliberately broken ones, and checks stage 1 reacts correctly to each.
#
# Usage: bash tests/boot/test-stage1.sh   (or: make test-boot)
set -uo pipefail
cd "$(dirname "$0")/../.."

out_dir=build/test-boot
mkdir -p "$out_dir"
failures=0

# --- build ---------------------------------------------------------------------
nasm -f bin -o "$out_dir/stub.bin" tests/boot/stage2-stub.asm || exit 1
s2=$(( ($(stat -c %s "$out_dir/stub.bin") + 511) / 512 ))
nasm -f bin -DSTAGE2_SECTORS=$s2 -DDISK_SECTORS=$((1 + s2)) \
    -o "$out_dir/stage1.bin" boot/stage1.asm || exit 1

size=$(stat -c %s "$out_dir/stage1.bin")
if [ "$size" -ne 512 ]; then
    echo "FAIL  stage1.bin is $size bytes, expected 512"; exit 1
fi
free=$(python3 -c "import sys; d=open(sys.argv[1],'rb').read()[:440]; print(len(d)-len(d.rstrip(b'\0')))" "$out_dir/stage1.bin")
echo "info  stage 1: $s2-sector stage 2, ~$free of 440 code bytes free"

# --- images --------------------------------------------------------------------
cat "$out_dir/stage1.bin" "$out_dir/stub.bin" > "$out_dir/good.img"

# Correct size, but stage 2 is all zeros: magic check must reject it.
cp "$out_dir/stage1.bin" "$out_dir/bad-magic.img"
truncate -s $(( (1 + s2) * 512 )) "$out_dir/bad-magic.img"

# Only the MBR: reading stage 2 runs off the end of the disk.
cp "$out_dir/stage1.bin" "$out_dir/truncated.img"

# --- cases ---------------------------------------------------------------------
# run_case NAME IMAGE TIMEOUT WANT_STATUS WANT_REGEX
#   WANT_STATUS 124 = still running (halted) when the timeout hit
run_case() {
    local name=$1 img=$2 secs=$3 want_rc=$4 want_re=$5 out rc
    out=$(timeout "$secs" bash vm/qemu.sh --headless --image "$img" 2>&1 | tr -d '\r')
    rc=${PIPESTATUS[0]}
    if [ "$rc" -eq "$want_rc" ] && grep -Eq "$want_re" <<<"$out"; then
        echo "PASS  $name"
        grep -E "$want_re" <<<"$out" | sed 's/^/      /'
    else
        echo "FAIL  $name (status $rc, want $want_rc; output must match: $want_re)"
        sed 's/^/      /' <<<"$out"
        failures=$((failures + 1))
    fi
}

run_case "loads stage 2 and hands over DL + T0" "$out_dir/good.img" 15 33 \
    '^stage2-stub: OK drive=80 stage1_ticks=0x[0-9a-f]{16}$'
run_case "rejects stage 2 without LKX2 magic" "$out_dir/bad-magic.img" 4 124 \
    '^LKX stage1: bad stage 2$'
run_case "reports a disk read error" "$out_dir/truncated.img" 4 124 \
    '^LKX stage1: disk read error$'

echo
if [ "$failures" -eq 0 ]; then echo "stage 1: all tests passed"; else echo "stage 1: $failures failed"; exit 1; fi

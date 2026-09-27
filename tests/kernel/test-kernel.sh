#!/usr/bin/env bash
# LiteKern X — kernel entry tests. Boots the real image in the headless VM and
# checks the boot log; boots a self-test build that faults on purpose and
# checks the exception is caught and reported.
#
# The kernel never exits QEMU itself (it has no business poking a debug port
# on real hardware), so each boot runs until a line matches, then is killed.
#
# Usage: bash tests/kernel/test-kernel.sh   (or: make test-kernel)
set -uo pipefail
cd "$(dirname "$0")/../.."

failures=0

make -s build/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-fault EXTRA_CFLAGS=-DLKX_SELFTEST_FAULT build/test-fault/litekernx.img >/dev/null || exit 1

# boot_until IMAGE REGEX SECONDS [EXTRA QEMU ARGS] -> $out holds the serial output
boot_until() {
    local img=$1 re=$2 secs=$3 log pid i
    shift 3
    log=$(mktemp)
    bash vm/qemu.sh --headless --image "$img" -- "$@" >"$log" 2>&1 &
    pid=$!
    for ((i = 0; i < secs * 10; i++)); do
        grep -Eq "$re" "$log" && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    sleep 0.3       # REGEX matched the last expected line; let the rest of the output land
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    out=$(tr -d '\r' <"$log" | grep -v '^qemu-system-i386: terminating')
    rm -f "$log"
}

# check NAME: all remaining args are regexes that must each match a line;
# a regex prefixed with ! must match no line.
check() {
    local name=$1 re ok=1
    shift
    for re in "$@"; do
        if [ "${re:0:1}" = "!" ]; then
            grep -Eq "${re:1}" <<<"$out" && ok=0
        else
            grep -Eq "$re" <<<"$out" || ok=0
        fi
    done
    if [ $ok -eq 1 ]; then
        echo "PASS  $name"
    else
        echo "FAIL  $name"
        failures=$((failures + 1))
    fi
    sed '/^$/d; s/^/      /' <<<"$out"
}

# Last line of a full boot / of a panic. Anchored to the end of the line so a
# half-written line doesn't count.
done_re='^pci: [0-9]+ devices on [0-9]+ buses.?$|^PANIC: .*\).?$'

boot_until build/litekernx.img "$done_re" 20
check "boots to ready with per-phase timing" \
    '^LiteKern X$' \
    '^\[boot\] tsc=[0-9]+ MHz' \
    '^\[boot\] t=[0-9]+ phase=bootloader dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=vbe dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=kernel_early dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=pci dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=first_frame dt=[0-9]+$' \
    '^\[boot\] ready t=[0-9]+$' \
    '!PANIC|exception'

check "reports the BIOS memory map and display mode" \
    '^mem 0x0000000000000000-0x[0-9a-f]{16} usable$' \
    '^mem 0x0000000000100000-0x[0-9a-f]{16} usable$' \
    '^mem: 10[0-9]{2} MiB usable in [0-9]+ regions$' \
    '^fb 1024x768x32 pitch=4096 at 0x[0-9a-f]{8}$'

check "enumerates QEMU's PCI devices" \
    '^pci 00:00\.0 8086:1237 class 06\.00\.00 rev [0-9a-f]{2} host bridge$' \
    '^pci 00:01\.0 8086:7000 class 06\.01\.00 rev [0-9a-f]{2} ISA bridge$' \
    '^pci 00:01\.1 8086:7010 class 01\.01\.80 rev [0-9a-f]{2} IDE controller$' \
    '^pci 00:02\.0 1234:1111 class 03\.00\.00 rev [0-9a-f]{2} VGA controller$' \
    '^pci: [0-9]+ devices on 1 buses$'

boot_until build/litekernx.img "$done_re" 20 \
    -device pci-bridge,chassis_nr=1,id=br1 -device virtio-rng-pci,bus=br1,addr=3
check "follows a PCI-to-PCI bridge to its secondary bus" \
    '^pci 00:[0-9a-f]{2}\.0 1b36:0001 class 06\.04\.00 rev [0-9a-f]{2} PCI bridge$' \
    '^pci 01:03\.0 1af4:1005 ' \
    '^pci: [0-9]+ devices on 2 buses$'

boot_until build/test-fault/litekernx.img "$done_re" 20
check "catches and reports a CPU exception (#UD self-test)" \
    '^exception 6 \(#UD invalid opcode\) error=0x00000000$' \
    '^  eip=0x001[0-9a-f]{5} cs=0x0008 ' \
    '^PANIC: unhandled CPU exception 6 ' \
    '!ready'

echo
if [ "$failures" -eq 0 ]; then echo "kernel: all tests passed"; else echo "kernel: $failures failed"; exit 1; fi

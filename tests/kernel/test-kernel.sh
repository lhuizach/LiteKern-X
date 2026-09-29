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

selftest_src=tests/kernel/selftest_drivers.c
make -s build/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-fault EXTRA_CFLAGS=-DLKX_SELFTEST_FAULT build/test-fault/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-drivers EXTRA_CFLAGS=-DLKX_SELFTEST_DRIVERS EXTRA_KERNEL_SRCS=$selftest_src \
    build/test-drivers/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-bad-driver "EXTRA_CFLAGS=-DLKX_SELFTEST_DRIVERS -DLKX_SELFTEST_BAD_DRIVER" \
    EXTRA_KERNEL_SRCS=$selftest_src build/test-bad-driver/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-user EXTRA_CFLAGS=-DLKX_SELFTEST_USER \
    "EXTRA_KERNEL_SRCS=tests/kernel/selftest_user.c tests/kernel/user_programs.asm" \
    build/test-user/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-kernel-null EXTRA_CFLAGS=-DLKX_SELFTEST_KERNEL_NULL \
    build/test-kernel-null/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-kernel-wp EXTRA_CFLAGS=-DLKX_SELFTEST_KERNEL_WP \
    build/test-kernel-wp/litekernx.img >/dev/null || exit 1
make -s BUILD=build/diag-vbios EXTRA_CFLAGS=-DLKX_DIAG_VBIOS build/diag-vbios/litekernx.img \
    >/dev/null || exit 1

# The 1024x600 video BIOS patch, tested on QEMU's q35 (945-style PAM registers)
# with a video BIOS carrying the EeePC's Intel mode table. SeaBIOS on q35
# boots through AHCI, which won't read a disk image this small: pad it.
mkdir -p build/test-boot
python3 tests/boot/make-fake-intel-vbios.py /usr/share/seabios/vgabios-stdvga.bin \
    build/test-boot/fake-intel-vgabios.bin >/dev/null || exit 1
cp build/diag-vbios/litekernx.img build/test-boot/diag-1m.img
truncate -s 1M build/test-boot/diag-1m.img

# boot_until IMAGE REGEX SECONDS [EXTRA QEMU ARGS] -> $out holds the serial output
# Once REGEX matches:
#   KEYS="a shift-a ret"  types those keys, then waits for KEYS_DONE (a regex)
#   SCREEN="check ..."    takes a screenshot and appends tests/lib/qemu_monitor.py's
#                         "screen: ..." lines for those checks to $out
boot_until() {
    local img=$1 re=$2 secs=$3 log pid i sock=/tmp/lkx-test-mon-$$.sock
    local shot=build/test-screens/$(basename "$(dirname "$img")").png
    shift 3
    log=$(mktemp)
    rm -f "$sock"
    bash vm/qemu.sh --headless --image "$img" -- -monitor "unix:$sock,server,nowait" "$@" >"$log" 2>&1 &
    pid=$!
    for ((i = 0; i < secs * 10; i++)); do
        grep -Eq "$re" "$log" && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done
    sleep 0.3       # REGEX matched the last expected line; let the rest of the output land
    if [ -n "${KEYS:-}" ]; then
        python3 tests/lib/qemu_monitor.py keys "$sock" $KEYS
        for ((i = 0; i < 50; i++)); do
            grep -Eq "$KEYS_DONE" "$log" && break
            sleep 0.1
        done
        sleep 0.2
    fi
    if [ -n "${SCREEN:-}" ]; then
        mkdir -p build/test-screens
        python3 tests/lib/qemu_monitor.py shot "$sock" "$shot" &&
            screen_out=$(python3 tests/lib/qemu_monitor.py check "$shot" $SCREEN)
    fi
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    out=$(tr -d '\r' <"$log" | grep -v '^qemu-system-i386: terminating')
    [ -n "${SCREEN:-}" ] && out+=$'\n'"${screen_out:-screen: no screenshot}"
    rm -f "$log" "$sock"
}

NAVY=1e3a5f TEXT=c8d0dc RED=801010 WHITE=ffffff

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

# Last line of a full boot / of the driver self-test / of a panic. Anchored to
# the end of the line so a half-written line doesn't count.
done_re='^mouse: \(512, 384\) buttons ---.?$|^PANIC: .*\).?$'
selftest_done_re='^selftest: drivers [0-9]+/[0-9]+ passed.?$|^PANIC: .*\).?$'
user_done_re='^selftest: user [0-9]+/[0-9]+ passed.?$|^PANIC: .*[0-9a-f)].?$'
panic_re='^PANIC: .*[0-9a-f)].?$'

SCREEN="corner= has=$TEXT@0,16,100,32" boot_until build/litekernx.img "$done_re" 20
check "boots to ready with per-phase timing" \
    '^LiteKern X$' \
    '^\[boot\] tsc=[0-9]+ MHz' \
    '^\[boot\] t=[0-9]+ phase=bootloader dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=vbe dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=kernel_early dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=paging dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=pci dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=drivers dt=[0-9]+$' \
    '^\[boot\] t=[0-9]+ phase=first_frame dt=[0-9]+$' \
    '^\[boot\] ready t=[0-9]+$' \
    '!PANIC|exception'

check "reports the BIOS memory map and display mode" \
    '^mem 0x0000000000000000-0x[0-9a-f]{16} usable$' \
    '^mem 0x0000000000100000-0x[0-9a-f]{16} usable$' \
    '^mem: 10[0-9]{2} MiB usable in [0-9]+ regions$' \
    '^fb 1024x768x32 pitch=4096 at 0x[0-9a-f]{8}$' \
    "^vbe: VBE 3\.0, 'SeaBIOS VBE\(C\) 2011', 16384 KiB, [0-9]+ modes seen; 32 bpp LFB:.* 800x600 1024x768" \
    '!vbe: patched|vbe: WARNING'

check "turns on paging with the planned layout" \
    '^mm: paging on; null page unmapped; kernel code read-only 0x00100000-0x[0-9a-f]{8}$' \
    '^mm: RAM identity-mapped to 0x40000000; user space 0x80000000-0xbfffffff$' \
    '^mm: framebuffer mapped 0xfd000000-' \
    '^mm: [0-9]+ MiB free of [0-9]+ MiB managed$'

check "enumerates QEMU's PCI devices" \
    '^pci 00:00\.0 8086:1237 class 06\.00\.00 rev [0-9a-f]{2} host bridge$' \
    '^pci 00:01\.0 8086:7000 class 06\.01\.00 rev [0-9a-f]{2} ISA bridge$' \
    '^pci 00:01\.1 8086:7010 class 01\.01\.80 rev [0-9a-f]{2} IDE controller$' \
    '^pci 00:02\.0 1234:1111 class 03\.00\.00 rev [0-9a-f]{2} VGA controller$' \
    '^pci: [0-9]+ devices on 1 buses$'

check "binds the COM1, display, keyboard and touchpad drivers" \
    '^dev com1 driver=uart16550 bound$' \
    '^dev fb0 driver=vbefb bound$' \
    '^dev kbd0 driver=ps2kbd bound$' \
    '^dev mouse0 driver=ps2mouse bound$' \
    '^drivers: 4 registered, 4 devices bound, 0 failed; [0-9]+ PCI devices without a driver$'

check "shows the boot log on screen (navy = ready)" \
    '^console: 128x48 characters, video BIOS font at 0x[0-9a-f]{5}$' \
    "^screen: corner $NAVY$" \
    "^screen: has $TEXT in 0,16,100,32: yes$"

QEMU_MACHINE=q35 boot_until build/test-boot/diag-1m.img "$done_re" 20 \
    -vga none -device VGA,romfile=build/test-boot/fake-intel-vgabios.bin
check "patches an Intel video BIOS mode table to 1024x600 (q35, EeePC table)" \
    '^vbe: patched the Intel video BIOS: mode 0x5c is now 1024x600 \(panel native\)$' \
    '^ 30/8/9a1a/00  32/8/9a34/00  34/8/9a4e/00  3c/8/9a68/00  5c/32/9a68/00$' \
    '^res \+9a68: 68 5b 00 a8 42 58 3c 20 ' \
    '^    t1 1024x600 ' \
    '^    t1 800x600 ' \
    '^diag: host 8086:29c0 pam 90-96 = 10 11 11 ' \
    '!vbe: WARNING|PANIC'

KEYS="a shift-a caps_lock b caps_lock 1 shift-1 ret up left ctrl-c esc backspace" \
KEYS_DONE='^kbd: key 0x00e ascii 0x08' \
    boot_until build/litekernx.img "$done_re" 20
check "keyboard: IRQ-driven key events decoded (shift, caps lock, ctrl, E0 keys)" \
    "^kbd: key 0x01e 'a'$" \
    "^kbd: key 0x02a mods 0x1$" \
    "^kbd: key 0x01e 'A' mods 0x1$" \
    "^kbd: key 0x030 'B' mods 0x8$" \
    "^kbd: key 0x002 '1'$" \
    "^kbd: key 0x002 '!' mods 0x1$" \
    '^kbd: key 0x01c ascii 0x0a$' \
    '^kbd: key 0x148$' \
    '^kbd: key 0x14b$' \
    '^kbd: key 0x02e ascii 0x03 mods 0x2$' \
    '^kbd: key 0x001 ascii 0x1b$' \
    '^kbd: key 0x00e ascii 0x08$' \
    '!PANIC'

KEYS="move:40,30 move:-10,0 press:1 release press:2 release press:4 release move:0,-900 move:5000,0" \
KEYS_DONE='^mouse: \(1023, 0\) buttons ---' \
    boot_until build/litekernx.img "$done_re" 20
check "touchpad: IRQ 12 packets decoded (direction, buttons, clamped at the edges)" \
    '^mouse: \(552, 414\) buttons ---$' \
    '^mouse: \(542, 414\) buttons ---$' \
    '^mouse: \(542, 414\) buttons L--$' \
    '^mouse: \(542, 414\) buttons --R$' \
    '^mouse: \(542, 414\) buttons -M-$' \
    '^mouse: \(1023, 0\) buttons ---$' \
    '!PANIC'

KEYS="a b c d e f g h i j k l m home" KEYS_DONE="^kbd: key 0x026 'l'" \
SCREEN="has=$TEXT@0,0,200,16 has=$TEXT@800,0,1024,16 has=$TEXT@0,16,100,32" \
    boot_until build/litekernx.img "$done_re" 20
check "scrollback: Home shows the start of the log with the indicator" \
    "^screen: has $TEXT in 0,0,200,16: no$" \
    "^screen: has $TEXT in 800,0,1024,16: yes$" \
    "^screen: has $TEXT in 0,16,100,32: yes$" \
    '!kbd: key 0x147'

boot_until build/test-drivers/litekernx.img "$selftest_done_re" 20
check "driver layer + display driver self-test" \
    '^dev pci 00:01\.1 driver=selftest-ide bound$' \
    '^dev pci 00:02\.0 driver=selftest-fail FAILED \(EIO\)$' \
    '^dev com3 driver=uart16550 FAILED \(ENODEV\)$' \
    '^selftest: hello through the com1 driver$' \
    '^selftest: drivers ([0-9]+)/\1 passed$' \
    '!selftest: FAIL|PANIC'

boot_until build/test-bad-driver/litekernx.img "$selftest_done_re" 20
check "refuses a driver with a missing operation" \
    "^PANIC: driver selftest-bad: missing operation 'read' " \
    '!selftest: drivers'

boot_until build/test-user/litekernx.img "$user_done_re" 30
check "ring 3 programs: syscalls work, bad pointers refused, faults kill only the program" \
    '^user: hello from ring 3$' \
    '^user: killed by exception 14 \(#PF page fault\) at eip=0x8' \
    '^  page fault: user read from 0x00100000 \(protection violation\)$' \
    '^  page fault: user write to 0x00000000 \(page not present\)$' \
    '^user: killed by exception 13 \(#GP general protection\)' \
    '^selftest: user ([0-9]+)/\1 passed$' \
    '!selftest: FAIL|PANIC'

SCREEN="corner= has=$WHITE@0,16,100,32" boot_until build/test-kernel-null/litekernx.img "$panic_re" 20
check "a kernel null-pointer write panics (page 0 unmapped), log shown on red" \
    '^  page fault: kernel write to 0x00000000 \(page not present\)$' \
    '^PANIC: page fault in the kernel at 0x00000000$' \
    "^screen: corner $RED$" \
    "^screen: has $WHITE in 0,16,100,32: yes$"

boot_until build/test-kernel-wp/litekernx.img "$panic_re" 20
check "a kernel write to its own code panics (code is read-only)" \
    '^  page fault: kernel write to 0x001[0-9a-f]{5} \(protection violation\)$' \
    '^PANIC: page fault in the kernel at 0x001[0-9a-f]{5}$'

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

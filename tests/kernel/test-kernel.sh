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
make -s BUILD=build/test-gfx EXTRA_CFLAGS=-DLKX_SELFTEST_GFX EXTRA_KERNEL_SRCS=tests/kernel/selftest_gfx.c \
    build/test-gfx/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-wm EXTRA_CFLAGS=-DLKX_SELFTEST_WM EXTRA_KERNEL_SRCS=tests/kernel/selftest_wm.c \
    build/test-wm/litekernx.img >/dev/null || exit 1
# A kernel whose packed wallpaper has one byte damaged: it must notice (the
# checksum, or inflate's own checks) and fall back to the plain colour.
rm -f build/test-wallpaper-bad/gen/wallpaper.lkxw     # fresh each run: flipping twice undoes it
make -s BUILD=build/test-wallpaper-bad build/test-wallpaper-bad/gen/wallpaper.lkxw >/dev/null || exit 1
python3 -c "
import sys; p = sys.argv[1]; d = bytearray(open(p, 'rb').read())
d[len(d) // 2] ^= 0x5a; open(p, 'wb').write(bytes(d))" build/test-wallpaper-bad/gen/wallpaper.lkxw
make -s BUILD=build/test-wallpaper-bad build/test-wallpaper-bad/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-disk EXTRA_CFLAGS=-DLKX_SELFTEST_DISK \
    EXTRA_KERNEL_SRCS=tests/kernel/selftest_disk.c build/test-disk/litekernx.img >/dev/null || exit 1
rm -f build/test-apps/litekernx.img build/test-apps/stage1.bin     # FAT_MB isn't a make dependency
make -s BUILD=build/test-apps FAT_MB=64 "EXTRA_APPS=tests/apps/crash tests/apps/badcalls tests/apps/hang" \
    build/test-apps/litekernx.img >/dev/null || exit 1
make -s BUILD=build/test-widgets EXTRA_CFLAGS=-DLKX_SELFTEST_WIDGETS \
    EXTRA_KERNEL_SRCS=tests/kernel/selftest_widgets.c build/test-widgets/litekernx.img >/dev/null || exit 1

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
# The desktop's bottom-right corner: the wallpaper's bottom edge colour (the
# 1024x600 image is centred on QEMU's 1024x768 screen). PLAIN: no wallpaper.
DESKTOP=$(sed -n 's/^bottom=//p' build/gen/wallpaper.lkxw.txt) PLAIN=202634

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

SCREEN="corner= has=$WHITE@0,0,120,30 has=3584e4@422,698,482,758" boot_until build/litekernx.img "$done_re" 20
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

check "binds the drivers (COM1, display, keyboard, touchpad, clock, disks)" \
    '^dev com1 driver=uart16550 bound$' \
    '^dev fb0 driver=vbefb bound$' \
    '^dev kbd0 driver=ps2kbd bound$' \
    '^dev mouse0 driver=ps2mouse bound$' \
    '^dev rtc0 driver=cmos-rtc bound$' \
    '^dev boot0 driver=bios-disk bound$' \
    '^ata0: no internal disk \(only the one we booted from\)$' \
    '^drivers: 7 registered, 6 devices bound, 1 failed; [0-9]+ PCI devices without a driver$'

check "boots to the desktop (top bar, dock, wallpaper)" \
    '^console: 128x48 characters, video BIOS font at 0x[0-9a-f]{5}$' \
    '^wallpaper: 1024x600, [0-9]+ KB packed, unpacked and checked in [0-9]+ ms$' \
    '^lkx: Files \(apps/files/files\.lkx, [0-9]+ KB\)$' \
    "^screen: corner $DESKTOP$" \
    "^screen: has $WHITE in 0,0,120,30: yes$" \
    '^screen: has 3584e4 in 422,698,482,758: yes$' \
    '^fb0: write-combining on \(MTRR 0xfd000000-0xfd3fffff\)$'

SCREEN="corner= has=ffffff@16,16,200,32 has=e8a33d@376,48,428,88 has=48a6e8@16,110,500,290" \
    boot_until build/test-gfx/litekernx.img '^selftest: gfx [0-9]+/[0-9]+ passed.?$|^PANIC: .*[0-9a-f)].?$' 20
check "rendering pipeline: primitives, clipping, damage, present (+ pattern on screen)" \
    '^selftest: gfx ([0-9]+)/\1 passed$' \
    '^gfx: bench 1024x768: ' \
    "^screen: corner $NAVY$" \
    '^screen: has ffffff in 16,16,200,32: yes$' \
    '^screen: has e8a33d in 376,48,428,88: yes$' \
    '^screen: has 48a6e8 in 16,110,500,290: yes$' \
    '!selftest: FAIL|PANIC'

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

BLACK=000000
KEYS="move:40,30 move:-10,0" KEYS_DONE='^mouse: \(542, 414\) buttons ---' \
SCREEN="has=$BLACK@542,414,560,440 has=$BLACK@505,380,540,410" \
    boot_until build/litekernx.img "$done_re" 20
check "cursor: the arrow follows the touchpad, nothing left behind" \
    "^screen: has $BLACK in 542,414,560,440: yes$" \
    "^screen: has $BLACK in 505,380,540,410: no$" \
    '!cursor: no|PANIC'

# Window system: the self-test leaves a window open; the cursor (starting at
# the centre, 512,384) is moved onto the close button (1001,53: below the
# 30 px top bar) and clicked.
SCREEN="has=222226@200,400,800,700 corner=" \
    boot_until build/test-wm/litekernx.img '^selftest: wm [0-9]+/[0-9]+ passed.?$|^PANIC: .*[0-9a-f)].?$' 20
check "window system: header bar, buttons, events (+ Adwaita dark window on screen)" \
    '^selftest: wm ([0-9]+)/\1 passed$' \
    '^screen: has 222226 in 200,400,800,700: yes$' \
    '^screen: corner 222226$' \
    '!selftest: FAIL|PANIC'

KEYS="move:489,-331 press:1 release" KEYS_DONE='^wm: close' SCREEN="corner=" \
    boot_until build/test-wm/litekernx.img '^mouse: \(512, 384\) buttons ---.?$' 20
check "window system: clicking the close button closes the window, the desktop comes back" \
    '^wm: close$' \
    "^screen: corner $DESKTOP$" \
    '!PANIC'

# Clicks are logged; the hidden log must not draw over the desktop (it did:
# each logged click scrolled the log onto the screen).
KEYS="move:30,30 press:1 release press:1 release press:1 release" KEYS_DONE='^mouse: \(542, 414\) buttons ---' \
SCREEN="corner= has=$TEXT@0,0,1024,768" \
    boot_until build/litekernx.img "$done_re" 20
check "desktop: logged clicks don't draw the log over it" \
    '^mouse: \(542, 414\) buttons L--$' \
    "^screen: corner $DESKTOP$" \
    "^screen: has $TEXT in 0,0,1024,768: no$" \
    '!PANIC'

# The shell on the 1024x768 screen. The pointer starts at 512,384.
#   dock (bottom, centred): Files 422-482, Log 482-542, Show Apps 542-602; y 698-758
#   top bar: Home 6-46, power 978-1018; y 3-27. Power menu: Restart 844-1012 x 40-74
#   app menu (two apps, centred above the dock): Files 400-512, Log 512-624; y 311-415
#   a window's close button: 1001,53
KEYS="move:-60,344 press:1 release move:549,-675 press:1 release" KEYS_DONE='^desktop: close Files' \
SCREEN="corner= has=3584e4@422,698,482,758" \
    boot_until build/litekernx.img "$done_re" 20
check "desktop: the dock opens Files (a ring 3 app); its close button ends it" \
    '^desktop: open Files$' \
    '^lkx: Files exited \(0\)$' \
    '^desktop: close Files$' \
    "^screen: corner $DESKTOP$" \
    '^screen: has 3584e4 in 422,698,482,758: yes$' \
    '!PANIC'

boot_until build/test-widgets/litekernx.img '^selftest: widgets [0-9]+/[0-9]+ passed.?$|^PANIC: .*[0-9a-f)].?$' 20
check "widgets: button, list, entry, dialog (states, input, results)" \
    '^selftest: widgets ([0-9]+)/\1 passed$' \
    '!selftest: FAIL|PANIC'

# Files on the real FAT32 partition, driven from the keyboard, on a scratch
# copy of the image: Ctrl+N creates, F2 renames (a duplicate name is refused),
# Enter opens a folder, Backspace goes back up, Delete deletes. Afterwards
# Linux's own tools check the partition in the image file.
mkdir -p build/test-files
cp build/litekernx.img build/test-files/disk.img
KEYS="move:-60,344 press:1 release ctrl-n h i ret f2 backspace backspace b y e ret ctrl-n b y e ret esc home ret ctrl-n n e w dot t x t ret backspace home down down delete ret" \
KEYS_DONE='^user: files: delete bye' boot_until build/test-files/disk.img "$done_re" 40
dd if=build/test-files/disk.img of=build/test-files/fat.img bs=512 skip=2048 status=none
out+=$'\n'$(fsck.fat -n build/test-files/fat.img >/dev/null 2>&1 && echo "fsck: clean" || echo "fsck: ERRORS")
out+=$'\n'$(MTOOLS_SKIP_CHECK=1 mdir -/ -b -i build/test-files/fat.img ::/ 2>/dev/null | sed 's/^/mtools: /')
check "Files: create, rename, delete and folders on the FAT32 partition (checked by fsck.fat)" \
    '^storage: boot0 partition at 2048 \(FAT32\): FAT32, read/write$' \
    '^user: files: open Boot disk \(LITEKERNX\)$' \
    '^user: files: create hi$' \
    '^user: files: rename hi -> bye$' \
    '^user: files: open folder Documents$' \
    '^user: files: create new\.txt$' \
    '^user: files: delete bye$' \
    '^fsck: clean$' \
    '^mtools: ::/Documents/new\.txt$' \
    '^mtools: ::/Documents/notes\.txt$' \
    '^mtools: ::/Welcome to LiteKern X\.txt$' \
    '!mtools: ::/(hi|bye)$' \
    '!user: files: create bye|^user: files: .* failed|PANIC'

# The Log app shows the log below the top bar and its header bar (30 + 46 px).
KEYS="move:0,344 press:1 release a b c d e f g h i j k l m home" KEYS_DONE="^kbd: key 0x026 'l'" \
SCREEN="has=$TEXT@0,76,200,92 has=$TEXT@800,76,1024,92 has=$TEXT@0,92,100,108" \
    boot_until build/litekernx.img "$done_re" 20
check "Log app: the log in a window; Home shows its start with the indicator" \
    '^desktop: open Log$' \
    "^screen: has $TEXT in 0,76,200,92: no$" \
    "^screen: has $TEXT in 800,76,1024,92: yes$" \
    "^screen: has $TEXT in 0,92,100,108: yes$" \
    '!kbd: key 0x147'

KEYS="move:60,344 press:1 release move:-4,-365 press:1 release" KEYS_DONE='^desktop: open Log' \
    boot_until build/litekernx.img "$done_re" 20
check "shell: Show Apps opens the app menu; clicking an app there opens it" \
    '^desktop: app menu open$' \
    '^desktop: open Log$' \
    '!PANIC'

KEYS="move:-60,344 press:1 release move:-426,-713 press:1 release" KEYS_DONE='^desktop: close Files' \
SCREEN="corner=" boot_until build/litekernx.img "$done_re" 20
check "shell: Home closes the open app" \
    '^desktop: open Files$' \
    '^lkx: Files exited \(0\)$' \
    '^desktop: close Files$' \
    "^screen: corner $DESKTOP$" \
    '!PANIC'

KEYS="move:486,-369 press:1 release move:-68,42 press:1 release" KEYS_DONE='^power: restarting' \
    boot_until build/litekernx.img "$done_re" 20
check "shell: the power menu restarts the machine" \
    '^shell: restart$' \
    '^power: restarting$' \
    '!PANIC'

# The clock, from the RTC: QEMU's clock starts just before the new year, so
# the first minute change rolls the day, month, year and weekday over.
boot_until build/litekernx.img '^shell: clock Fri 1 Jan  00:00.?$|^PANIC: .*[0-9a-f)].?$' 20 \
    -rtc base=2026-12-31T23:59:57
check "shell: the clock shows the RTC's date and time, and follows it" \
    '^shell: clock Thu 31 Dec  23:59$' \
    '^shell: clock Fri 1 Jan  00:00$' \
    '!PANIC'

SCREEN="corner=" boot_until build/test-wallpaper-bad/litekernx.img "$done_re" 20
check "a damaged wallpaper is caught: plain colour instead, no crash" \
    '^wallpaper: corrupt ' \
    "^screen: corner $PLAIN$" \
    '!PANIC'

# The boot disk through the BIOS: a scratch copy of the image with 1 MiB of
# zeros after it, so the self-test's write to the last sector hits padding.
# The write is then checked in the image file itself.
cp build/test-disk/litekernx.img build/test-disk/scratch.img
truncate -s +1M build/test-disk/scratch.img
boot_until build/test-disk/scratch.img '^selftest: disk [0-9]+/[0-9]+ passed.?$|^PANIC: .*[0-9a-f)].?$' 30
out+=$'\n'$(python3 -c "
import sys; d = open(sys.argv[1], 'rb').read()[-512:]
ok = d[:4] == b'LKXW' and all(d[i] == (i * 7 + 3) & 255 for i in range(4, 512))
print('image: last sector', 'written' if ok else 'NOT written')" build/test-disk/scratch.img)
check "BIOS disk: reads, refuses past the end, writes that reach the disk" \
    '^dev boot0 driver=bios-disk bound$' \
    '^selftest: disk ([0-9]+)/\1 passed$' \
    '^image: last sector written$' \
    '!selftest: FAIL|PANIC'

# The internal disk (ATA, read-only): a second IDE disk with a FAT32 and an
# "NTFS" partition. Files starts at Places (three partitions); the internal
# FAT32 one opens read-only (Ctrl+N does nothing), the NTFS one won't open.
# The disk image must come out byte for byte unchanged.
mkdir -p build/test-ata
bash tests/kernel/make-internal-disk.sh build/test-ata/internal.img
before=$(md5sum < build/test-ata/internal.img)
KEYS="move:-60,344 press:1 release down down ret ctrl-n x ret backspace down ret" \
KEYS_DONE='^user: files: .* is not supported' boot_until build/litekernx.img "$done_re" 30 \
    -drive file=build/test-ata/internal.img,format=raw,if=ide,index=1
[ "$before" = "$(md5sum < build/test-ata/internal.img)" ] && out+=$'\n'"image: unchanged" || out+=$'\n'"image: CHANGED"
check "ATA internal disk: found, FAT32 read-only, NTFS left alone, never written" \
    '^ata0: QEMU HARDDISK, [0-9]+ MiB, primary slave \(read-only\)$' \
    '^storage: ata0 partition at 2048 \(FAT32\): FAT32, read-only$' \
    '^storage: ata0 partition at 133120 \(NTFS or exFAT\): not supported, left alone$' \
    '^user: files: open Internal disk \(WINDOWS\)$' \
    '^user: files: Internal disk is not supported \(NTFS or exFAT\)$' \
    '^image: unchanged$' \
    '!user: files: create|PANIC'

# Misbehaving apps must never take the kernel down (Phase 2 section 5). The test
# image's dock: Files 332-392, Crash 392-452, Badcalls 452-512, Hang 512-572
# (y 698-758). Crash writes to address 0; Badcalls passes kernel pointers and
# nonsense to the calls; Hang spins until the watchdog stops it. Then Files
# must still open and work.
KEYS="move:-90,344 press:1 release sleep:1 move:60,0 press:1 release sleep:1 move:60,0 press:1 release sleep:12 move:-180,0 press:1 release sleep:1" \
KEYS_DONE='^user: files: open' boot_until build/test-apps/litekernx.img "$done_re" 40
check "apps: a crash, bad calls and a hang each end only the app" \
    '^user: killed by exception 14 \(#PF page fault\) at eip=0x8' \
    '^lkx: Crash crashed \(page fault\) and was stopped; the system carries on$' \
    '^user: badcalls: present before open -22$' \
    '^user: badcalls: unknown call -38$' \
    '^user: badcalls: list into kernel memory -14$' \
    '^user: badcalls: path in kernel memory -14$' \
    '^user: badcalls: bad volume -19$' \
    '^user: badcalls: dot-dot path -22$' \
    '^user: badcalls: missing folder -2$' \
    '^user: badcalls: bad name -22$' \
    '^user: badcalls: window into kernel memory -14$' \
    '^user: badcalls: header with 99 buttons -22$' \
    '^user: badcalls: font into kernel memory -14$' \
    '^lkx: Badcalls exited \(7\)$' \
    '^user: no response for 10 s at eip=0x8' \
    '^lkx: Hang stopped responding and was stopped$' \
    '^user: files: open Boot disk' \
    '!still running|PANIC'

# Stability (Phase 2 section 6): Files opened and closed 15 times, then a burst
# of keys and clicks 20 ms apart. Every app run must give back all of its
# memory, and Files must still work at the end. On a scratch copy of the image.
mkdir -p build/test-stress
cp build/litekernx.img build/test-stress/disk.img
stress="gap:0.1"
for i in $(seq 15); do
    stress+=" move:-60,344 press:1 release move:549,-675 press:1 release move:-489,331"
done
stress+=" move:-60,344 press:1 release sleep:1 gap:0.02"
for i in $(seq 60); do stress+=" down up"; done
for i in $(seq 30); do stress+=" press:1 release"; done
stress+=" home end pgdn pgup ctrl-n a b c esc f2 esc delete esc gap:0.15 sleep:0.5 ctrl-n o k ret"
KEYS="$stress" KEYS_DONE='^user: files: create ok' boot_until build/test-stress/disk.img "$done_re" 90
opens=$(grep -c '^desktop: open Files$' <<<"$out")
mem=$(grep '^lkx: memory free' <<<"$out" | sort -u | wc -l)
out+=$'\n'"stress: $opens opens; $mem distinct memory lines"
out+=$'\n'$(grep '^lkx: memory free' <<<"$out" | awk '{ if ($4 != $7) bad = 1 } END { print bad ? "stress: LEAK" : "stress: no leak" }')
check "stability: 16 opens and closes, a fast input burst, no leak, still working" \
    '^stress: 16 opens; 1 distinct memory lines$' \
    '^stress: no leak$' \
    '^user: files: create ok$' \
    '!PANIC|crashed|stopped responding'

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

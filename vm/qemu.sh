#!/usr/bin/env bash
# LiteKern X — QEMU VM approximating the ASUS EeePC 1000HE.
#
# This is a development VM, not a hardware-accurate emulator:
#   real 1000HE                       this VM
#   Atom N270 @1.6GHz, 1 core + HT    -cpu n270, 1 core (SMP/HT is a Non-Goal)
#   1 GB DDR2                         -m 1G
#   945GSE / ICH7-M                   i440FX / PIIX (-machine pc)
#   GMA 950, 1024x600 panel           Bochs VBE (-vga std), no native 1024x600 VBE mode
#   SATA in IDE/compat mode           IDE disk
#   PS/2 keyboard + touchpad (i8042)  PS/2 keyboard + mouse (i8042)
#   COM1: none                        COM1 -> terminal + build/serial.log
#
# Timings measured in this VM do NOT count against the boot budget —
# only the real EeePC does (see docs/BOOT-BUDGET.md).
set -euo pipefail

usage() {
    cat <<'EOF'
usage: vm/qemu.sh [--image FILE] [--headless | --debug] [-- EXTRA QEMU ARGS]

  --image FILE   raw disk image to boot (default: build/litekernx.img)
  --headless     no window; serial on stdout; isa-debug-exit enabled so a
                 guest can end the run with a status code (used by tests)
  --debug        window + gdb stub on :1234, paused at reset; interrupts,
                 CPU resets and guest errors logged to build/qemu-debug.log
EOF
}

image=build/litekernx.img
mode=gui
extra=()
while [ $# -gt 0 ]; do
    case "$1" in
        --image)    image="$2"; shift 2 ;;
        --headless) mode=headless; shift ;;
        --debug)    mode=debug; shift ;;
        --)         shift; extra=("$@"); break ;;
        -h|--help)  usage; exit 0 ;;
        *)          usage >&2; exit 2 ;;
    esac
done

if [ ! -f "$image" ]; then
    echo "vm/qemu.sh: image not found: $image" >&2
    exit 1
fi
mkdir -p build

args=(
    -name "LiteKern X (EeePC 1000HE profile)"
    -machine pc
    -cpu n270
    -smp 1
    -m 1G
    -vga std
    -rtc base=localtime
    -drive "file=$image,format=raw,if=ide,index=0,media=disk"
    -chardev "stdio,id=com1,logfile=build/serial.log"
    -serial chardev:com1
    -no-reboot
)

case "$mode" in
    headless)
        # Guest writes V to port 0xf4 -> QEMU exits with status (V << 1) | 1.
        args+=(-display none -device isa-debug-exit,iobase=0xf4,iosize=0x04)
        ;;
    debug)
        args+=(-s -S -no-shutdown -d int,cpu_reset,guest_errors -D build/qemu-debug.log)
        echo "gdb stub on localhost:1234, VM paused. In gdb: target remote :1234" >&2
        ;;
esac

exec qemu-system-i386 "${args[@]}" "${extra[@]}"

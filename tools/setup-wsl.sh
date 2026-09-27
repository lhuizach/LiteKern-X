#!/usr/bin/env bash
# LiteKern X — installs the Phase 1 toolchain and QEMU in WSL Ubuntu.
# Only what Phase 1 needs: ISO tooling (grub-mkrescue, xorriso) waits for Phase 4.
#
# Usage (from the project folder): sudo bash tools/setup-wsl.sh
set -euo pipefail

pkgs=(
    build-essential   # gcc, ld, make
    gcc-multilib      # gcc -m32 for freestanding i386 code
    nasm              # bootloader / low-level asm
    gdb               # debugging against QEMU's gdb stub
    qemu-system-x86   # qemu-system-i386
    qemu-system-gui   # GTK window (shown on Windows through WSLg)
)

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo bash tools/setup-wsl.sh" >&2
    exit 1
fi

apt-get update
apt-get install -y "${pkgs[@]}"
echo "Done. Check with: make check-tools"

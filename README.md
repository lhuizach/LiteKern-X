# LiteKern X

A from-scratch 32-bit x86 OS for the ASUS EeePC 1000HE (Intel Atom N270). It's a clean-slate rewrite of LiteKern v1 that keeps the KERN86 app model (`.lkx`, `kerns.json`, `kern86.h`).

**Status:** Phase 1, sections 1–3 are done in the VMs. The bootloader is stage 1 + stage 2. The C kernel sets up its GDT/IDT, calibrates the TSC, reports the memory map, display mode and every PCI device, logs per-phase boot times, and reaches "ready". None of this has been verified on the real EeePC yet.

On real hardware there's no serial port, so the screen colour is the only status signal for now: **navy means ready, dark red means panic**.

- Plan: [`LiteKernX-Roadmap/`](LiteKernX-Roadmap/). The phase files there are the source of truth.
- [Non-Goals](docs/NON-GOALS.md)
- [Boot-time budget](docs/BOOT-BUDGET.md): ≤ 1000 ms from bootloader entry to first frame

## Layout
```
LiteKernX-Roadmap/   the plan, phase by phase
boot/                custom bootloader (stage 1 MBR, stage 2) + boot_info layout — see docs/BOOT-PROTOCOL.md
kernel/              C kernel: entry, GDT/IDT, exceptions, TSC timing, memory map, PCI scan, serial log
docs/                non-goals, boot budget, boot protocol
tests/boot/          bootloader tests + the stage 2 / kernel stubs they boot
tests/kernel/        kernel tests (boot log, memory map, PCI incl. bridges, exception self-test)
vm/qemu.sh           QEMU dev VM configured to approximate the EeePC 1000HE
vm/vbox.sh           VirtualBox dev VM (same profile + a 1024x600 mode)
vm/smoke/            boot-sector smoke test for the VM itself
tools/setup-wsl.sh   installs the toolchain + QEMU in WSL Ubuntu
```
Other source directories (`drivers/`, …) get created when the phase that needs them starts, not before.

## Setup (Windows + WSL Ubuntu)
Run these once, from PowerShell in this folder:
```
wsl sudo bash tools/setup-wsl.sh
wsl make check-tools
```

## Dev VM
| Command | What it does |
|---|---|
| `wsl make` | Builds `build/litekernx.img` (stage 1 + stage 2 + kernel) |
| `wsl make test` | Runs all tests: the VM smoke test, the bootloader tests (good boot and every failure path) and the kernel tests |
| `wsl make smoke` | Boots the smoke image headless and checks serial output + exit code |
| `wsl make smoke-gui` | Same image in a QEMU window (shown through WSLg) |
| `wsl make run` | Builds and boots `build/litekernx.img` in a window |
| `wsl make debug` | Same as `run`, paused, with a gdb stub on `:1234`; interrupts and resets logged to `build/qemu-debug.log` |

### VirtualBox VM
There's a second dev VM, `LiteKern X`, in your Windows VirtualBox. It has the same EeePC-like profile as the QEMU VM, plus a **1024×600** VBE mode like the real panel, which QEMU can't provide. It's driven from WSL by [`vm/vbox.sh`](vm/vbox.sh):

| Command | What it does |
|---|---|
| `wsl make vbox-create` | Creates and registers the VM (once) |
| `wsl make vbox` | Rebuilds, copies the image to the VM's disk, and boots it in a window |
| `wsl make vbox-test` | Same, but headless: waits for `[boot] ready`, saves `build/vbox/screen.png`, powers off |

The VM's boot log (COM1) goes to `build/vbox/serial.log`. `wsl bash vm/vbox.sh stop|status|destroy` manage the VM. The VM has to be powered off before its disk can be updated.

### Serial output
In the QEMU VM, serial output (COM1) always goes to the terminal and to `build/serial.log`. See the header of [`vm/qemu.sh`](vm/qemu.sh) for how the VM differs from the real EeePC. **VM timings don't count toward the boot budget.**

## Toolchain
Host `gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib`, GNU `ld` and `nasm`, all in WSL. A proper `i686-elf` cross-compiler is a Non-Goal until host gcc causes real problems.

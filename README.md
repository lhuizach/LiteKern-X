# LiteKern X

A from-scratch 32-bit x86 OS for the ASUS EeePC 1000HE (Intel Atom N270). It's a clean-slate rewrite of LiteKern v1 that keeps the KERN86 app model (`.lkx`, `kerns.json`, `kern86.h`).

**Status:** Phase 1, section 1 (project setup). Nothing boots yet.

- Plan: [`LiteKernX-Roadmap/`](LiteKernX-Roadmap/). The phase files there are the source of truth.
- [Non-Goals](docs/NON-GOALS.md)
- [Boot-time budget](docs/BOOT-BUDGET.md): ≤ 1000 ms from bootloader entry to first frame

## Layout
```
LiteKernX-Roadmap/   the plan, phase by phase
docs/                non-goals, boot budget, and design notes as they're written
vm/qemu.sh           QEMU dev VM configured to approximate the EeePC 1000HE
vm/smoke/            boot-sector smoke test for the VM (not the real bootloader)
tools/setup-wsl.sh   installs the toolchain + QEMU in WSL Ubuntu
```
Source directories (`boot/`, `kernel/`, `drivers/`, …) get created when the phase that needs them starts, not before.

## Setup (Windows + WSL Ubuntu)
Run these once, from PowerShell in this folder:
```
wsl sudo bash tools/setup-wsl.sh
wsl make check-tools
```

## Dev VM
| Command | What it does |
|---|---|
| `wsl make smoke` | Boots the smoke image headless and checks serial output + exit code |
| `wsl make smoke-gui` | Same image in a QEMU window (shown through WSLg) |
| `wsl make run` | Boots `build/litekernx.img` (once Phase 1 §2 creates it) |
| `wsl make debug` | Same as `run`, paused, with a gdb stub on `:1234`; interrupts and resets logged to `build/qemu-debug.log` |

Serial output (COM1) always goes to the terminal and to `build/serial.log`. See the header of [`vm/qemu.sh`](vm/qemu.sh) for how the VM differs from the real EeePC. **VM timings don't count toward the boot budget.**

## Toolchain
Host `gcc -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib`, GNU `ld` and `nasm`, all in WSL. A proper `i686-elf` cross-compiler is a Non-Goal until host gcc causes real problems.

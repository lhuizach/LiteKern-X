# LiteKern X — Non-Goals

> **Status: approved 2026-09-27.** Changes are made deliberately, by editing this file.

These are things LiteKern X is deliberately **not** building for v1.0. If you want to work on one of them, first finish the current phase. Then either keep it here, or move it into a phase file on purpose. Starting something that isn't in the plan without deciding to is exactly how v1 ended up the way it did.

## Platforms
- ARM64 / Raspberry Pi 4
- Samsung Galaxy S10e
- x86-64 (long mode). X is 32-bit only.
- Hardware other than the EeePC 1000HE (and the QEMU dev VM)

## Hardware / drivers
- Networking: Ethernet (Atheros) and Wi-Fi
- Audio (HD Audio)
- USB stack (UHCI/EHCI): the internal keyboard and touchpad are PS/2, and the BIOS handles booting from USB
- Webcam, SD card reader, Bluetooth
- Native GMA 950 modesetting or 2D acceleration. Use only the VBE linear framebuffer.
- Keyboard layouts other than US, and keyboard LEDs
- Touchpad extended protocols (Elantech/Synaptics), gestures and multi-touch. Basic 3-byte PS/2 packets only.
- SMP / Hyper-Threading. Only one logical CPU is used.
- ACPI power management: suspend, hibernate, battery status, backlight and Fn keys
- Drivers beyond the core set in Phase 1 §6

## Kernel
- Networking stack, sockets
- Multiple users, permissions, login
- POSIX compatibility, porting existing Unix software
- Dynamic linking and shared libraries for apps
- Swap / demand paging to disk

## GUI / apps
- Any GUI work before Phase 2, and any visual polish before Phase 3
- More than one KERN86 app before Phase 3, and at most 3 apps total for v1.0
- Porting v1 apps in bulk. Port one at a time, and only as part of a phase task.
- Theming or user customisation, localisation, fonts beyond one bitmap font

## Project
- Website or landing page work before Phase 4
- GRUB / Multiboot. X uses its own bootloader (decided 2026-09-27) so every millisecond of the boot path is under our control.
- A cross-compiler toolchain (i686-elf-gcc) for now. Host `gcc -m32 -ffreestanding` is enough until it clearly isn't.
- A disk driver and filesystem for v1.0 (decided 2026-09-27). `.lkx` apps ship in a read-only ramdisk inside the boot image, loaded by stage 2 alongside the kernel (built in Phase 2 §5). Nothing is written back to disk.

## Decided
| Question | Answer | Where it's built |
|---|---|---|
| Where do `.lkx` apps load from? | A read-only ramdisk in the boot image. No disk driver, no filesystem. | Phase 2 §5 |
| Screen resolution? | Native 1024×600, 32 bpp. The EeePC's video BIOS lacks that mode, so stage 2 patches its mode table first (`915resolution` style). Elsewhere it falls back to 1024×768, then 800×600. | Phase 1 §2 (done, verified on the EeePC 2026-09-29) |

## Open questions (decide before the named step, then move the answer to a phase file or to this list)
| Question | Decide by | Notes |
|---|---|---|
| One app at a time, or several running at once? | Phase 2 §3 | This decides whether a scheduler or task switching is needed. |

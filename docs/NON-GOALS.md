# LiteKern X — Non-Goals

> **Status: approved 2026-09-27, revised 2026-09-29.** Changes are made deliberately, by editing this file.
>
> **2026-09-29 revision:** removed from this list and moved into phase files:
> - Theming and user customisation, fonts beyond one bitmap font, and the 3-app cap → Phase 3 (`03-LiteKernX-Polish.md` §3, §5)
> - Networking, audio, USB, ACPI power management, multiple users, localisation and x86-64 → Phase 5 (`05-LiteKernX-Expansion.md`, after the v1.0 release)
>
> The same day, the boot budget was relaxed to ≤ 5 s to the desktop (`docs/BOOT-BUDGET.md`).

These are things LiteKern X is deliberately **not** building for v1.0. If you want to work on one of them, first finish the current phase. Then either keep it here, or move it into a phase file on purpose. Starting something that isn't in the plan without deciding to is exactly how v1 ended up the way it did.

## Platforms
- ARM64 / Raspberry Pi 4
- Samsung Galaxy S10e
- Hardware other than the EeePC 1000HE (and the QEMU dev VM)

## Hardware / drivers
- Webcam, SD card reader, Bluetooth
- Native GMA 950 modesetting or 2D acceleration. Use only the VBE linear framebuffer.
- Keyboard layouts other than US, and keyboard LEDs
- Touchpad extended protocols (Elantech/Synaptics), gestures and multi-touch. Basic 3-byte PS/2 packets only.
- SMP / Hyper-Threading. Only one logical CPU is used.
- Drivers beyond the core set in Phase 1 §6, except the ones a phase file plans (Phase 5: ACPI, audio, USB, networking)

## Kernel
- POSIX compatibility, porting existing Unix software
- Dynamic linking and shared libraries for apps
- Swap / demand paging to disk

## GUI / apps
- Any GUI work before Phase 2, and any visual polish before Phase 3
- Porting v1 apps in bulk. Port one at a time, and only as part of a phase task.

## Project
- Website or landing page work before Phase 4
- GRUB / Multiboot. X uses its own bootloader (decided 2026-09-27) so every millisecond of the boot path is under our control.
- A cross-compiler toolchain (i686-elf-gcc) for now. Host `gcc -m32 -ffreestanding` is enough until it clearly isn't.
- Filesystems other than FAT32 (NTFS, ext*, exFAT…), and writing to any disk or partition that isn't FAT32. A disk driver and FAT32 read/write **are** now in scope (decided 2026-09-30, for the Files app: Phase 2 §5). An NTFS internal disk is shown as unsupported, never touched.
- A USB storage stack for the boot stick. The kernel reaches it through the BIOS (a real-mode call per request), which only works for the drive the BIOS booted from. Other USB drives are Phase 5 §3.

## Decided
| Question | Answer | Where it's built |
|---|---|---|
| Where do `.lkx` apps load from? | A read-only ramdisk in the boot image. | Phase 2 §5 |
| One app at a time, or several running at once? (decided 2026-09-30) | **One at a time, full screen.** Opening an app closes the current one, so no scheduler or task switching. | Phase 2 §3 |
| Which disks can apps see? (decided 2026-09-30) | **The boot USB stick's FAT32 partition, read/write** (made by `make usb`; also readable on a Windows PC), and **the EeePC's internal disk read-only**, writable only where it's FAT32. | Phase 2 §5 |
| Look of the GUI? (decided 2026-09-30) | **Adwaita (Fedora/GNOME), dark style**: Adwaita's real colours and header bar. Simple for now; Phase 3 §5 redoes it properly (light style, accents, the anti-aliased font). | Phase 2 §3–§4 |
| Screen resolution? | Native 1024×600, 32 bpp. The EeePC's video BIOS lacks that mode, so stage 2 patches its mode table first (`915resolution` style). Elsewhere it falls back to 1024×768, then 800×600. | Phase 1 §2 (done, verified on the EeePC 2026-09-29) |

## Open questions (decide before the named step, then move the answer to a phase file or to this list)
| Question | Decide by | Notes |
|---|---|---|
| — | — | None open right now. |

# LiteKern X — Boot Protocol

This file is the contract between the custom bootloader stages. If you change it, change it here first.

## Disk layout
| LBA | Contents |
|---|---|
| 0 | Stage 1: MBR boot sector (`boot/stage1.asm`) |
| 1 … N | Stage 2 (N ≤ 64 sectors). Its size is baked into stage 1 at build time. |
| N+1 … | Kernel (defined when stage 2 is written) |

The MBR contains one partition entry so that BIOSes which only boot USB sticks with a valid partition table accept it. The entry is marked active, has type `0x7F` (the unofficial "OS development" type), and covers LBA 1 to the end of the image. The first 90 bytes of the MBR (the FAT BPB area) contain only a jump over them. Some BIOSes rewrite that area in memory when they emulate a USB stick as a floppy/ZIP drive.

## Stage 1 → stage 2 handoff
Stage 1 loads stage 2 to physical `0x8000` in a single BIOS `int 13h` extended (LBA) read. It retries 3 times with a disk reset in between. It then checks that the first 4 bytes are the magic `LKX2` and far-jumps to `0000:8004`.

On entry to stage 2:
| State | Value |
|---|---|
| CPU mode | 16-bit real mode, interrupts enabled |
| `CS DS ES SS` | `0` |
| `SP` | `0x7C00` (stack grows down) |
| `DL` | BIOS boot drive number |
| `[0x0500]` | qword: T0, the `rdtsc` value from stage 1's first instructions (see `BOOT-BUDGET.md`) |

Stage 1 **fails loudly** instead of guessing. It prints `LKX stage1: <reason>` to the screen, mirrors it to COM1 and halts. The possible reasons:
- `no LBA`: the BIOS lacks `int 13h` extensions. There's no CHS fallback.
- `disk read error`: stage 2 couldn't be read after 3 tries.
- `bad stage 2`: the magic check failed.

## Early memory map (real mode)
| Physical range | Use |
|---|---|
| `0x00000–0x004FF` | IVT + BIOS data area (don't touch) |
| `0x00500–0x00507` | T0 TSC (stage 2 copies it into its boot-info struct) |
| `0x07000–0x07BFF` | Bootloader stack |
| `0x07C00–0x07DFF` | Stage 1 (free once stage 2 is running) |
| `0x08000–0x0FFFF` | Stage 2 (max 32 KiB, so it can't cross a 64 KiB DMA boundary) |

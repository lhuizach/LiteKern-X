# LiteKern X — Boot Protocol

This file is the contract between the custom bootloader stages and the kernel. If you change it, change it here first. The structures are defined in `boot/bootinfo.inc` (assembly) and `boot/bootinfo.h` (C). The C header has static asserts, and `make test-boot` checks that the two agree.

## Disk layout
| LBA | Contents |
|---|---|
| 0 | Stage 1: MBR boot sector (`boot/stage1.asm`) |
| 1 … N | Stage 2 (`boot/stage2.asm`), padded to whole sectors, N ≤ 64. Its size is baked into stage 1 at build time. |
| N+1 … | Kernel image: a kernel header followed by a flat binary. Stage 2 computes this LBA from its own size. |

The MBR contains one partition entry so that BIOSes which only boot USB sticks with a valid partition table accept it. The entry is marked active, has type `0x7F` (the unofficial "OS development" type), and covers LBA 1 to the end of the image. The first 90 bytes of the MBR (the FAT BPB area) contain only a jump over them. Some BIOSes rewrite that area in memory when they emulate a USB stick as a floppy/ZIP drive.

## Stage 1 → stage 2
Stage 1 loads stage 2 to physical `0x8000` in a single BIOS `int 13h` extended (LBA) read. It retries 3 times with a disk reset in between. It then checks that the first 4 bytes are the magic `LKX2` and far-jumps to `0000:8004`.

| State on entry to stage 2 | Value |
|---|---|
| CPU mode | 16-bit real mode, interrupts enabled |
| `CS DS ES SS` | `0` |
| `SP` | `0x7C00` (stack grows down) |
| `DL` | BIOS boot drive number |
| `[0x0500]` | qword: T0, the `rdtsc` value from stage 1's first instructions (see `BOOT-BUDGET.md`) |

## Stage 2 → kernel
Stage 2 runs these steps in order:
1. Initialise COM1 at 115200 8N1.
2. Read the E820 memory map (at most 32 entries).
3. Enable A20. It tries three things in turn: checking whether A20 is already on, then BIOS `int 15h AX=2401`, then port `0x92`.
4. Read the kernel header, validate it, then read the whole image into the bounce buffer at `0x10000` in 64-sector (32 KiB) chunks.
5. Set the VBE mode.
6. Switch to 32-bit protected mode, copy the kernel to its load address, and zero its bss.

**Kernel header** (`struct kernel_header`, the first 24 bytes of the image):

| Offset | Field | Rule |
|---|---|---|
| 0 | `magic` | `LKXK` |
| 4 | `version` | `1` |
| 8 | `load_addr` | Must be ≥ `0x100000` |
| 12 | `entry` | Must satisfy `load_addr ≤ entry < load_addr + file_size` |
| 16 | `file_size` | Bytes in the image, header included. Must be ≤ 448 KiB, the size of the bounce buffer. |
| 20 | `mem_end` | End of the kernel in memory. Must be ≥ `load_addr + file_size` and ≤ 16 MiB. Everything between the end of the file and `mem_end` gets zeroed (the bss). |

**Video mode:** stage 2 walks the VBE mode list once. It only accepts 32 bpp, direct-colour modes with a linear framebuffer. Its preferences are 1024×600 (the EeePC panel), then 1024×768, then 800×600, and it stops walking at the first 1024×600 it finds. QEMU has no 1024×600 mode, so the VM runs at 1024×768.

| State on entry to the kernel | Value |
|---|---|
| CPU mode | 32-bit protected mode, paging off, interrupts **disabled** |
| GDT | Stage 2's GDT: `0x08` is flat ring 0 code and `0x10` is flat ring 0 data (base 0, limit 4 GiB). It lives inside stage 2, so the kernel must load its own GDT before reusing low memory. |
| Segments | `CS=0x08`, `DS ES FS GS SS = 0x10` |
| `ESP` | `0x7C00`. That's valid, but it's a small stack in low memory, so the kernel should switch to its own. |
| `EAX` | `0x42584B4C` (`'LKXB'`) |
| `EBX` | Physical address of `struct boot_info` (`0x0600`) |
| A20 | Enabled |
| Video | VBE graphics mode, described in `boot_info.fb_*` |

**`struct boot_info`** holds the following. See `boot/bootinfo.h` for the exact layout.
- Its magic and version.
- The boot drive and flags. `BI_FLAG_FB` means the framebuffer fields are valid. `BI_FLAG_MMAP_TRUNCATED` means the BIOS reported more than 32 memory-map entries.
- 5 TSC marks: stage 1 entry (T0), stage 2 entry, kernel loaded, VBE mode set, and kernel entry. The kernel converts them to ms once it has calibrated the TSC.
- The E820 map's address and entry count.
- The framebuffer's address, pitch, width, height and bpp.
- The kernel's start address and `mem_end`.

## Failures
Every stage **fails loudly** instead of guessing. It prints `LKX stage<n>: <reason>` to the screen, mirrors it to COM1 and halts. Once the VBE mode is set, only COM1 shows these messages, but no failure paths exist after that point.

| Message | Cause |
|---|---|
| `LKX stage1: no LBA` | The BIOS lacks `int 13h` extensions. There's no CHS fallback. |
| `LKX stage1: disk read error` | Stage 2 couldn't be read after 3 tries. |
| `LKX stage1: bad stage 2` | Stage 2's `LKX2` magic check failed. |
| `LKX stage2: E820 memory map unavailable` | The BIOS returned no E820 entries. |
| `LKX stage2: cannot enable A20` | All three A20 methods failed. |
| `LKX stage2: kernel read error` | A kernel sector couldn't be read after 3 tries. |
| `LKX stage2: bad kernel header` | The header broke one of the rules above. |
| `LKX stage2: kernel too big` | The image is larger than the 448 KiB bounce buffer. |
| `LKX stage2: no usable VBE mode (need 32 bpp LFB)` | No acceptable video mode was found. |

## Early memory map
| Physical range | Use |
|---|---|
| `0x00000–0x004FF` | IVT + BIOS data area (don't touch) |
| `0x00500–0x00507` | T0 TSC (stage 2 copies it into `boot_info`) |
| `0x00600–0x0065B` | `struct boot_info` |
| `0x00700–0x009FF` | E820 entries (32 × 24 bytes) |
| `0x03000–0x032FF` | VBE info scratch (stage 2 only) |
| `0x07000–0x07BFF` | Bootloader stack. The kernel's entry `ESP` points at its top. |
| `0x07C00–0x07DFF` | Stage 1 (free once stage 2 is running) |
| `0x08000–0x0FFFF` | Stage 2, including the GDT used at kernel entry (max 32 KiB) |
| `0x10000–0x7FFFF` | Kernel bounce buffer (free once the kernel is running) |
| `0x100000–` | Kernel (`load_addr` … `mem_end`, below 16 MiB) |

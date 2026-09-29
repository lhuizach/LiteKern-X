# LiteKern X — Boot-Time Budget

**Target: ≤ 5 s to the desktop, with something on screen the whole time. Hard ceiling: 10 s.** (v1 was ~5000 ms.)

> **Changed 2026-09-29.** The original target was ≤ 1000 ms, and Phase 1 met it (179 ms on the EeePC). That limit is dropped: a boot that shows good output (a splash, progress, the log) is worth a few seconds. What still matters is that it's a laptop that gets switched on and off often, so boot has to stay in seconds, never minutes. A boot that looks frozen is not OK, even if it's short: from the first frame on, the screen must show progress.

## What counts
- **Start (T0):** first instruction of the LiteKern X bootloader.
- **End:** the desktop is up and accepts input. Phase 1 ended at the first frame / kernel "ready" state; that's still logged as `ready`.
- **Excluded:** BIOS POST and the BIOS handing off to the boot sector. We can't control those. Time them once on the real EeePC with a stopwatch, just for reference.
- **Authoritative hardware:** only the real EeePC 1000HE counts toward the budget. QEMU timings are useful for spotting trends (e.g. "this change doubled PCI time") but aren't the real numbers.

## How it's measured
- The bootloader reads `rdtsc` at T0 and passes it to the kernel. Every phase boundary reads `rdtsc` again.
- The N270's TSC runs at a constant rate. Calibrate it against the PIT once during early boot so ticks convert to ms, and log that calibration.
- Every phase boundary logs one line to COM1 in a fixed format that scripts can parse:
  ```
  [boot] t=<ms since T0> phase=<name> dt=<ms for this phase>
  ```
  In the VM this goes to the terminal and to `build/serial.log`. On the EeePC, which has no serial port, the same lines go to an on-screen log once the framebuffer is up (they're buffered until then).
- Phases are **recorded while booting and printed together at the end** (`boot_report()`), so no phase includes time spent writing to the serial port. In the VMs, serial output is slow enough to swamp everything else: printing the PCI list inline made `pci` look like 171 ms when the scan itself takes under 1 ms.

## Allocation

The core phases below were the Phase 1 plan against the old 1000 ms total and are all well inside it (see the log). Keep them roughly there: the extra time is meant for things you can **see** (splash, wallpaper, desktop), not for slow hardware setup nobody notices. If a phase goes over, either get it back under or consciously move budget from slack. Don't let overruns pile up quietly.

| # | Phase (`phase=`) | Covers | Budget |
|---|---|---|---|
| 1 | `bootloader` | Boot sector → kernel loaded into memory | 150 ms |
| 2 | `vbe` | VBE mode query + mode set (still in real mode) | 150 ms |
| 3 | `kernel_early` | Kernel entry, stack, GDT/IDT, memory map | 50 ms |
| 4 | `paging` | Page tables, rings, TSS, syscall gate | 50 ms |
| 5 | `pci` | PCI bus enumeration | 50 ms |
| 6 | `drivers` | Driver matching + `init()` for all core drivers | 200 ms |
| 7 | `first_frame` | First complete frame presented | 100 ms |
| 8 | `assets` | Loading and unpacking the splash, wallpaper, icons and fonts (Phase 2+) | 1500 ms |
| 9 | `desktop` | Splash → desktop: shell, first app, any fade-in (Phase 2+) | 1500 ms |
| — | slack | Held back for surprises | 1250 ms |
|   | **Total** | | **5000 ms** (ceiling 10 s) |

## Known risks
- **PS/2 resets are slow.** A full keyboard/mouse reset (`0xFF`) plus self-test can take hundreds of ms on real hardware. Avoid full resets if the BIOS has already initialised the i8042, or run them without blocking the boot.
- **VBE calls on real hardware** go through the GMA 950 video BIOS and can be slow. Stage 2 walks the mode list once and stops at the first 1024×600 match. If `loaded->vbe` turns out to be large on the EeePC, hardcode that machine's 1024×600 mode number and try it first.
- **The first frame is now a full console draw:** a screen fill plus one blit per character already logged. In the VMs this takes 5–31 ms. On the EeePC the VBE framebuffer is probably uncached, so every pixel write goes straight to the bus. If `first_frame` is large there, the fix is to draw into a RAM buffer and blit it once (Phase 2's renderer does this anyway), or to map the framebuffer write-combining.
- **Disk reads through BIOS `int 13h`** are slow per call, and slower still when the BIOS emulates a USB stick as a disk. Load the kernel and ramdisk in as few large reads as possible. Big images (a raw 1024×600 wallpaper is 2.4 MB) are the main risk to the new total: compress them, and measure `assets` on the real EeePC before adding more.

## Measurements log
| Date | Build | Hardware | Total | Notes |
|---|---|---|---|---|
| 2026-09-27 | kernel entry | QEMU (TCG) — not authoritative | 56 ms | bootloader 6, vbe 3, kernel_early 46 (includes the 10 ms TSC calibration) |
| 2026-09-27 | kernel entry | VirtualBox 7.2.8, 1024×600 — not authoritative | 41 ms | bootloader 1, vbe 26, kernel_early 13 |
| 2026-09-27 | §3 hw detection (logging excluded) | QEMU (TCG) — not authoritative | 45 ms | bootloader 14, vbe 5, kernel_early 23, pci 0, first_frame 1 |
| 2026-09-27 | §3 hw detection (logging excluded) | VirtualBox 7.2.8, 1024×600 — not authoritative | 115 ms | bootloader 3, vbe 73, kernel_early 11, pci 3, first_frame 22 |
| **2026-09-29** | **Phase 1 complete (`d875977`)** | **EeePC 1000HE (real), 800×600 (see note)** | **179 ms** | **bootloader 16, vbe 73, kernel_early 10, paging 2, pci 0, drivers 39, first_frame 36.** First authoritative measurement: 18% of the (then) 1000 ms budget. TSC 1662 MHz. |

Every phase is inside its allocation. Worth knowing:
- `drivers` (39 ms) is almost entirely the PS/2 keyboard and touchpad answering commands. Real PS/2 devices take milliseconds to ACK, while QEMU answers instantly.
- `vbe` (73 ms) is the video BIOS walking its mode list. It's the biggest single cost.

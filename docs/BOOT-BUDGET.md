# LiteKern X — Boot-Time Budget

**Target: ≤ 1000 ms** (v1 was ~5000 ms).

## What counts
- **Start (T0):** first instruction of the LiteKern X bootloader.
- **End:** first frame on screen / kernel "ready" state.
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

## Allocation

These are first guesses. **Replace them with real measurements as soon as Phase 1 §2 boots.** If a phase goes over, either get it back under or consciously move budget from slack. Don't let overruns pile up quietly.

| # | Phase (`phase=`) | Covers | Budget |
|---|---|---|---|
| 1 | `bootloader` | Boot sector → kernel loaded into memory | 150 ms |
| 2 | `vbe` | VBE mode query + mode set (still in real mode) | 150 ms |
| 3 | `kernel_early` | Kernel entry, stack, GDT/IDT, memory map | 50 ms |
| 4 | `paging` | Page tables, rings, TSS, syscall gate | 50 ms |
| 5 | `pci` | PCI bus enumeration | 50 ms |
| 6 | `drivers` | Driver matching + `init()` for all core drivers | 200 ms |
| 7 | `first_frame` | First complete frame presented | 100 ms |
| — | slack | Held back for surprises | 250 ms |
|   | **Total** | | **1000 ms** |

## Known risks
- **PS/2 resets are slow.** A full keyboard/mouse reset (`0xFF`) plus self-test can take hundreds of ms on real hardware. Avoid full resets if the BIOS has already initialised the i8042, or run them without blocking the boot.
- **VBE calls on real hardware** go through the GMA 950 video BIOS and can be slow. Stage 2 walks the mode list once and stops at the first 1024×600 match. If `loaded->vbe` turns out to be large on the EeePC, hardcode that machine's 1024×600 mode number and try it first.
- **Disk reads through BIOS `int 13h`** are slow per call. Load the kernel in as few large reads as possible.

## Measurements log
| Date | Build | Hardware | Total | Notes |
|---|---|---|---|---|
| 2026-09-27 | kernel entry | QEMU (TCG) — not authoritative | 56 ms | bootloader 6, vbe 3, kernel_early 46 (includes the 10 ms TSC calibration) |

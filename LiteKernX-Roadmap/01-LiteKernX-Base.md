# LiteKern X — Phase 1: Base (Core Kernel + Drivers)
**Budget: ~13 hrs**

Goal: a booting kernel with a working driver layer and basic hardware detection — nothing pretty yet, just solid ground to build on. Nothing in Phase 2 starts until this phase is genuinely done.

---

## 1. Project setup (~30 min)
- [x] New repo/branch for X — don't import v1 code wholesale
- [x] One-page Non-Goals list (features you're explicitly NOT doing yet) — `docs/NON-GOALS.md`
- [x] Boot-time budget documented: **target ≤ 1000ms** (v1 was ~5000ms) — `docs/BOOT-BUDGET.md`
- [x] QEMU dev VM approximating the EeePC (`vm/qemu.sh`), verified by `make smoke` passing
  - **Measured from bootloader entry → first frame / "ready" state.** BIOS POST on the EeePC is outside our control and is excluded (record it separately with a stopwatch for reference, but don't count it against the budget)
  - The VBE mode set happens in the bootloader (real mode), so its cost lands in the bootloader phase timing — expect it to be one of the larger line items

## 2. Minimal boot path (~2 hrs)
- [ ] **Custom bootloader** (decided 2026-09-27, no GRUB) — no unnecessary probing, no "just in case" delays
  - [x] Stage 1: 512-byte MBR boot sector, loads stage 2 with BIOS `int 13h` extended reads (LBA) — `boot/stage1.asm`, contract in `docs/BOOT-PROTOCOL.md`, tested by `make test-boot` (VM only; real-EeePC USB boot still to verify)
  - [x] Stage 2: reads the E820 memory map, sets the VBE mode, enables A20, loads the kernel in as few large reads as possible, switches to 32-bit protected mode, and jumps to the kernel with a boot-info struct (T0 TSC, memory map, framebuffer info) — `boot/stage2.asm`, tested by `make test-boot` against a kernel stub (VM only)
  - [x] Real-hardware check (2026-09-29): boots from USB on the EeePC; bootloader 16 ms, VBE 73 ms. **But stage 2 picked 800×600, not the panel's 1024×600** — the GMA 950 video BIOS doesn't offer a 32 bpp 1024×600 mode. Open issue, see below
- [x] Kernel entry + minimal init (stack, GDT/IDT, memory map read from BIOS) — `kernel/`; exceptions are reported and halt; memory map arrives via `boot_info` (printing it is §3)
- [x] Add per-phase boot timestamps from the start, so cost is visible immediately — don't leave perf measurement until the end like v1 did (use `rdtsc` from bootloader entry; calibrate against the PIT once so ticks convert to ms)

## 3. Hardware detection / enumeration (~1.5 hrs)
- [x] Read BIOS-provided memory map and basic display mode (this is your zero-driver framebuffer + memory info) — `kernel/memmap.c`
- [x] PCI bus enumeration — walk the bus, read vendor ID / device ID / class code for each device found — `kernel/pci.c`, follows PCI-to-PCI bridges (the EeePC's Ethernet/Wi-Fi sit behind PCIe root ports), results kept in `pci_devices[]` for §4
- [x] Log/print what's detected, even if nothing is initialized yet — this proves enumeration works independently of drivers (verified in QEMU, incl. a bridge, and VirtualBox)
- [x] Real-hardware check (2026-09-29): 17 devices on 5 buses, including the Atheros Ethernet (`1969:1026`, bus 3) and Wi-Fi (`168c:002a`, bus 1) behind the PCIe root ports — bridge-following works on the real chipset

## 4. Driver abstraction layer (~2 hrs)
- [x] Define the fixed driver interface — implemented in `kernel/driver.h` (the source of truth; it adds a `pci_ids` match list to `driver_t` and keeps the PCI info behind `dev->pci`). Every call takes a `device_t *` so one driver can be bound to a specific enumerated device (and, later, to more than one), and `ioctl` gives non-stream devices like the framebuffer a clean escape hatch instead of abusing `read`/`write`:
  ```c
  typedef struct device device_t;   /* bound device: PCI/legacy info + driver-private state */

  typedef struct {
      const char *name;
      int  (*init)(device_t *dev);
      int  (*read)(device_t *dev, void *buf, size_t len);
      int  (*write)(device_t *dev, const void *buf, size_t len);
      int  (*ioctl)(device_t *dev, unsigned cmd, void *arg);  /* e.g. FB_GET_INFO, FB_MAP */
      void (*shutdown)(device_t *dev);
  } driver_t;

  struct device {
      const driver_t *drv;
      uint16_t vendor_id, device_id;  /* 0 for legacy (non-PCI) devices like the i8042 */
      uint8_t  bus, slot, func;
      void    *priv;                  /* driver-owned state */
  };
  ```
- [x] Unsupported operations return a defined error (e.g. `-ENOSYS`), never a silent no-op — `driver_nosys_*` helpers; a NULL operation panics at `driver_add()`
- [x] Driver registry — kernel only ever calls through this interface (`dev_read/write/ioctl/shutdown`)
- [x] Registration fails loudly if `init()` doesn't succeed — no silent half-working drivers (device marked FAILED with its error, reported at boot, every later call returns `-ENODEV`)
- [x] Vendor/device ID → driver matching table (even a tiny hardcoded one) so enumeration results (step 3) can select a driver; legacy devices (i8042, VBE framebuffer) are registered explicitly — `driver_probe_pci()`, `device_add_legacy()`, built-ins listed in `drivers/builtin.c`
- [x] One reference driver, actually exercised: `drivers/uart.c` (16550, COM1). Checked by an 18-point self-test build (`tests/kernel/selftest_drivers.c`) plus a missing-operation panic test. On the EeePC, which has no UART, it should report `com1 FAILED (ENODEV)`

## 5. Memory protection + system calls (~3.5 hrs)
- [x] Set up paging (or segmentation, but paging is the standard modern approach) on the N270 — it supports it — `kernel/pmm.c` (bitmap frame allocator) + `kernel/vmm.c` (layout in `vmm.h`: null page unmapped, kernel code read-only with CR0.WP, RAM identity-mapped supervisor-only, user space 0x80000000–0xBFFFFFFF)
- [x] Kernel mode / user mode separation (CPU rings) — including a TSS so ring 3 → ring 0 transitions get a valid kernel stack — `kernel/gdt.c`, `kernel/usermode.asm` (a dedicated trap stack, so a trap never lands on live kernel frames)
- [x] Basic page fault handler — even if it just halts cleanly for now, it must not corrupt kernel state — goes further: a fault in ring 3 kills only that program and the kernel carries on (`kernel/idt.c`, `kernel/user.c`); a fault in ring 0 panics with the fault decoded
- [x] **System call entry** — a single `int 0x80` gate (DPL 3) with a syscall number table. This is how KERN86 apps in ring 3 will reach the kernel in Phase 2; without it ring separation has no front door — `kernel/syscall.h` documents the ABI
  - [x] Validate user pointers before the kernel touches them — `user_check()`; the self-test caught a wrap-around bug in it before it shipped
  - [x] Start with 2–3 trivial syscalls (e.g. `write` to debug log, `exit`) and prove a ring 3 stub can call them — `exit`, `debug_write`, `uptime_ms`; 12-check ring 3 self-test passes in QEMU and VirtualBox
- [x] This is the step that turns "code that runs" into "an OS" — don't skip it even though it's the least visually rewarding part

## 6. Core drivers, one at a time (~3.5 hrs)
Port from v1 deliberately, not wholesale. Suggested order:
1. [x] Display/framebuffer driver (through the new interface — VBE linear framebuffer, exposed via `ioctl`) — `drivers/vbefb.c`, device `fb0`; `FB_GET_INFO` / `FB_FILL_RECT` / `FB_BLIT` (clipped), interface in `kernel/fb.h`; 9 display checks in the driver self-test
   - [x] On-screen boot log (`kernel/console.c`), drawn only through `fb0`, using the video BIOS 8×16 font stage 2 locates (`boot_info` v2 `font_addr`). Replays everything logged since boot; background shows the status (navy ready / red panic). A debug console, not GUI work — it's what makes the EeePC (no serial port) debuggable
   - [x] Real-hardware check (2026-09-29): the boot log shows on the EeePC, scrollback works — at 800×600 (100×37 characters), not 1024×600; see the open issue
2. [x] i8042 controller + keyboard driver — `drivers/i8042.c` (shared with the touchpad: bounded waits, no slow resets), `drivers/kbd.c` (device `kbd0`, IRQ 1, scancode set 1 → `struct key_event` in `kernel/input.h`, US layout, non-blocking `read`). Brought in hardware IRQs: `kernel/irq.c` (8259 PICs remapped to 32–47, lines masked until a driver registers, spurious IRQ 7/15 handled); after boot the kernel idles with interrupts on and logs key presses. Tested by typing into QEMU (`sendkey`) and VirtualBox (`keyboardputstring`)
   - [x] Real-hardware check (2026-09-29): typed keys appear as `kbd: key ...` lines
3. [x] Touchpad/mouse driver (PS/2 aux port on the same i8042 — basic 3-byte PS/2 packets are enough; Elantech/Synaptics extended modes are a Non-Goal). Needed by Phase 2's cursor — `drivers/mouse.c` (device `mouse0`, IRQ 12, `struct mouse_event` in `kernel/input.h`, screen convention dy > 0 = down, packet resync + overflow discard). The idle loop tracks a pointer position and logs clicks and (rate-limited) movement. Tested by moving/clicking QEMU's mouse through the monitor; binds in VirtualBox
   - [x] Real-hardware check (2026-09-29): touchpad movement and both buttons appear as `mouse: ...` lines
4. [x] ~~Disk/storage driver, if needed at this stage~~ — not needed: apps ship in a read-only ramdisk in the boot image (decided 2026-09-27, see `docs/NON-GOALS.md`)

Each driver: implements full `driver_t` interface, tested in isolation, fails loudly not silently.

---

## Phase 1 Done Criteria
All of Phase 1's code is done and tested in QEMU and VirtualBox (2026-09-27), and on the EeePC 1000HE (2026-09-29, `docs/HARDWARE-TEST.md`). One box remains: the display resolution.

- [x] Kernel boots, reaches "ready" state, with per-phase timing logged (measured from bootloader entry) — EeePC: **ready at 179 ms** (budget 1000 ms)
- [x] PCI enumeration prints detected devices independent of any driver — EeePC: 17 devices, 5 buses
- [x] Driver registry works — display, keyboard, and touchpad drivers registered and callable — EeePC: `fb0`, `kbd0`, `mouse0` bound; `com1 FAILED (ENODEV)` as expected (no UART)
- [x] Paging + ring separation active — user/kernel mode actually enforced — EeePC (2026-09-29): ring 3 reading kernel memory and writing through null killed by #PF, port I/O and `cli` killed by #GP, kernel carried on
- [x] A ring 3 test stub successfully makes a syscall and returns, and a bad pointer from ring 3 is rejected rather than crashing the kernel — EeePC: `selftest: user 12/12 passed`, all six bad-pointer kinds got -EFAULT, 100 runs with no frame leak
- [ ] No known broken/half-finished features present — if it's not done, it's not in this build — **open: the display runs at 800×600 (stretched on the 1024×600 panel) because the video BIOS has no 1024×600 mode. Must be fixed or consciously accepted before Phase 2 (a GUI laid out for the wrong resolution is the kind of drift X exists to avoid)**

**Do not start Phase 2 (GUI) until every box above is checked.**

# LiteKern X — Phase 5: Expansion
**Budget: not set yet. Estimate each section before starting it.**
**Prerequisite: Phase 4 fully done (see 04-LiteKernX-Publishing.md Done Criteria). v1.0 is released first.**

Goal: the things removed from `docs/NON-GOALS.md` on 2026-09-29 that are too big for v1.0. Each section is its own release (v1.1, v1.2, …). The One Rule still applies: **one section at a time, each finished before the next starts.** The order below is a suggestion, most useful on a laptop first; reorder deliberately, not by drift.

---

## 1. ACPI power management
- [ ] Clean shutdown and reboot through ACPI (not just "hold the power button") — **shutdown done early (2026-10-02, the user's request)**: `kernel/acpi.c` finds RSDP -> RSDT -> FADT -> DSDT and reads `_S5` with a minimal AML reader (one package of numbers), then writes SLP_TYP | SLP_EN to PM1a/PM1b; works in QEMU (tested) and VirtualBox. Still to do here: check it on the EeePC, and reboot through the FADT reset register (restart still uses the keyboard controller)
- [ ] Battery status: charge level and charging state, shown in the shell
- [ ] Backlight brightness and the Fn keys that control it
- [ ] Suspend to RAM (resume must restore the display mode and PS/2 devices)
- [ ] Decide first: a minimal hand-written AML reader for the EeePC's tables, or a ported interpreter (ACPICA)? This decides the size of the whole section — for shutdown the minimal reader was enough; battery and suspend need methods (_BST, _PTS), so the question stays open for them

## 2. Audio
- [ ] Intel HD Audio controller driver, through the driver layer
- [ ] Play a PCM buffer (enough for UI sounds and one app)
- [ ] Volume control, including the Fn keys

## 3. USB
- [ ] UHCI/EHCI host controller driver
- [ ] USB mass storage, so the OS can read and write the stick it booted from
- [ ] Persistent settings: save the Phase 3 §5 theme choices. Needs a filesystem decision first (the disk driver and filesystem are still a v1.0 non-goal)
- [ ] USB keyboard/mouse (HID) only if a real need turns up; the built-in ones are PS/2

## 4. Networking
- [ ] Atheros Ethernet driver (`1969:1026`, found on bus 3 in Phase 1 §3)
- [ ] Network stack: IPv4, ARP, DHCP, UDP, then TCP
- [ ] Socket syscalls for KERN86 apps, added only as an app needs them
- [ ] Wi-Fi (`168c:002a`) last, if at all: it needs firmware, 802.11 and WPA, and is by far the biggest item in this phase

## 5. Multiple users
- [ ] User accounts, login screen, and per-user settings (needs §3's writable storage)
- [ ] File permissions, if a filesystem exists by then

## 6. Localisation
- [ ] Keyboard layouts other than US (still a non-goal in `docs/NON-GOALS.md` until this section starts)
- [ ] Translatable UI strings; non-Latin text needs a font that covers it

## 7. x86-64 (long mode)
- [ ] **The EeePC's Atom N270 is 32-bit only and can't run 64-bit code.** This only makes sense together with new target hardware, and "hardware other than the EeePC" is still a non-goal. Decide the hardware first, then move it off `docs/NON-GOALS.md`
- [ ] 64-bit paging, GDT/IDT, syscall entry, and a 64-bit KERN86 ABI

---

## Phase 5 Done Criteria
Per section: it works on the real EeePC, has isolated tests, fails loudly rather than silently, and boot stays within `docs/BOOT-BUDGET.md`.

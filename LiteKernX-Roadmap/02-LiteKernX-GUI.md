# LiteKern X — Phase 2: GUI & User Look
**Budget: ~20 hrs**
**Prerequisite: Phase 1 fully done (see 01-LiteKernX-Base.md Done Criteria)**

Goal: a stable, usable GUI — not decorated yet (that's Phase 3), but functional, responsive, and not broken.

---

## 1. Rendering pipeline (~4 hrs)
- [x] Port framebuffer rendering from v1, but running through the Phase 1 display driver interface (not direct hardware access like v1 had) — v1's design kept (RAM back buffer + present only changed rectangles): `kernel/screen.c` presents through `fb0`'s `FB_BLIT`, with a merging damage list. v1's framebuffer write-combining is ported too (`kernel/mtrr.c`), now using the Intel SDM's safe update sequence and refusing ranges that would cover RAM. v1's pixel-aspect correction isn't needed (native 1024×600)
- [x] Basic primitives: fill rect, draw line, blit bitmap, draw text (even a crude bitmap font is fine for now) — `kernel/gfx.c`: fill, outline, line, opaque and alpha blit, text in the BIOS 8×16 font (`kernel/font.c`, shared with the console), all clipped. 23-check self-test with guard words around the surfaces (`tests/kernel/selftest_gfx.c`)
- [ ] Confirm rendering performance doesn't regress vs v1's best state — timings are measured by `litekernx-gfx-test.img` (`make usb`); needs an EeePC run (QEMU: full-screen present 2.5 ms, 32×32 present ~0 ms)

## 2. Cursor system (~2 hrs)
- [x] Cursors are PNG images now (`assets/cursors/`, rules in `docs/ASSET-PROMPTS.md` §3), converted at build time — replaces porting v1's SVG cursor rasteriser — `tools/cursors2c.py` turns `assets/cursors.json` + PNGs into C (the build fails on a bad size or hotspot); `kernel/cursor.c` shows the user's `arrow.png`
- [x] Cursor movement driven by the Phase 1 touchpad/mouse driver (keyboard fallback optional) — the cursor is the screen's overlay (`screen_set_overlay`): composited on the way out, never drawn into the back buffer, so a move costs two 32×32 updates and each display pixel is written once (no flicker). The console now draws through the screen too, so everything on screen shares one back buffer. Checked by 6 overlay self-checks and a screenshot test
- [x] Confirm no tearing/lag at this stage — this was already a v1 strength, don't lose it — **confirmed on the EeePC 2026-09-30: smooth.** The first report that day was ~10 fps: each logged pointer movement scrolled the whole console. Fixed: movement is logged to serial only, and the console scrolls by moving pixels

## 3. Window/surface model (~4 hrs)
- [x] Decide: single fullscreen surface for now, or a real windowing model? (Given hardware constraints, simple is fine — don't over-scope) — **decided 2026-09-30: one app at a time, full screen**, in the Adwaita (GNOME/Fedora) dark style: a header bar with the app's title and buttons, the app's content below
- [x] Basic input routing — keyboard/mouse events reach the right surface — `kernel/wm.c`: while a window is open, keys and content clicks (in content coordinates) go to it as events; header-bar buttons report their id; the close button asks the app to close. The Adwaita dark theme is `kernel/theme.c` (token names as planned for Phase 3 §5). 15-check self-test + an end-to-end test that clicks the close button through the emulated touchpad
- [x] Redraw/damage handling so you're not redrawing the whole screen every frame (this matters a lot on Atom-class hardware) — from §1: only changed rectangles are presented; hovering a header button redraws just that button
- [x] **Desktop** (added 2026-09-30): after boot, a GNOME-style desktop replaces the log — a black top bar and an app grid (48×48 PNG icons, `assets/icons/`, rules in `docs/ASSET-PROMPTS.md` §4). Clicking an app opens it full screen; closing it returns to the desktop. For now the apps are built in (`kernel/apps.c`): **Files** (a placeholder until §5a) and **Log** (the boot log in a window). A panic still takes over the whole screen. §5 swaps the built-in list for KERN86 apps from `kerns.json`. Kept minimal: Phase 3 redoes it

## 4. Core GUI widgets (~5 hrs)
- [ ] Button, label, basic layout container — minimum set, not a full toolkit
- [ ] Each widget: state (hover/pressed/disabled), not just static appearance
- [ ] Keep this minimal — Phase 3 is where these get polished, not here

## 5a. Storage for the Files app (~8 hrs, added 2026-09-30)
Disks came off the Non-Goals list for the first app, Files (see `docs/NON-GOALS.md`, Decided).
- [ ] Block device interface through `driver_t` (read/write sectors by LBA via `ioctl`)
- [ ] **Boot USB stick:** reached through the BIOS with a real-mode call per request (port v1's `boot/realmode_thunk.asm` + `drivers/bios_disk.c` deliberately), read and write
- [ ] **Internal disk:** ATA PIO driver for the ICH7's IDE-compatible SATA (`8086:27c4`), read-only
- [ ] MBR partition table; `make usb` gives the stick a second partition, FAT32, that Windows can open too
- [ ] FAT32: list, read, create, delete, rename files and folders. Writes only ever go to FAT32 partitions, and are checked by a test that compares the result with Linux's own FAT tools
- [ ] An NTFS (or any non-FAT32) internal disk shows as "not supported" and is never written

## 5. KERN86 app integration (~3 hrs)
- [ ] Port `.lkx` loader, `kerns.json` manifest parsing, `kern86.h` API from v1
- [ ] Apps run in ring 3 — `kern86.h` becomes a thin user-side wrapper over the Phase 1 syscall gate (`int 0x80`), and the kernel side calls through the driver interface instead of v1's direct hardware access
- [ ] Extend the syscall table only as the test app needs it (draw/present surface, poll input, exit) — no speculative syscalls
- [ ] Get ONE minimal test app running end-to-end through the new GUI (doesn't need to be useful — proves load → manifest → API → render → input works) — **it's Files** (decided 2026-09-30): browse the USB stick and the internal disk, create, delete and rename files and folders on FAT32. Kept simple: every app is redone in Phase 3
- [ ] More apps may follow in this phase once that first one works end to end (the "one app before Polish" Non-Goal was removed 2026-09-29). Each has to meet the same bar: it loads from the ramdisk, runs in ring 3, and misbehaving never takes the kernel down.

## 6. Stability pass (~2 hrs)
- [ ] Stress test: open/close app repeatedly, resize (if applicable), rapid input — look for leaks or crashes
- [ ] Confirm page fault handler (Phase 1) actually catches bad app behavior instead of taking down the kernel

---

## Phase 2 Done Criteria
- [ ] GUI renders, responds to input, without crashing under normal use
- [ ] At least one real KERN86 app runs through the full stack
- [ ] Redraw is efficient — no full-screen redraw every frame in normal operation
- [ ] A misbehaving app doesn't crash the kernel (thanks to Phase 1's isolation work)

**Do not start Phase 3 (Polish) until every box above is checked.**

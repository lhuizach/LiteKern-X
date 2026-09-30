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
- [x] **Desktop** (added 2026-09-30): after boot, a GNOME-style desktop replaces the log. Clicking an app opens it full screen; closing it returns to the desktop. For now the apps are built in (`kernel/apps.c`): **Files** and **Log** (the boot log in a window). A panic still takes over the whole screen. §5 swaps the built-in list for KERN86 apps from `kerns.json`. Kept minimal: Phase 3 redoes it
- [x] **Shell: top bar, dock, app menu** (added 2026-09-30, decided with the user; `kernel/desktop.c`), Fedora-style:
  - **Top bar, always shown:** Home (closes the open app) and the open app's name on the left; day, date and 24-hour time in the middle, from the CMOS clock (new driver `drivers/rtc.c`, device `rtc0`, whose once-a-second interrupt updates it); a power menu on the right: Restart (`kernel/power.c`), Shut Down greyed out until ACPI (Phase 5)
  - **Wallpaper** (brought forward from Phase 3 §5 at the user's request, 2026-09-30): `assets/wallpapers/crossing.png` (`WALLPAPER=` in the Makefile) is packed at build time (`tools/wallpaper-pack.py`: PNG-style row filters + deflate, 156 KB, keeping the kernel under its 448 KiB limit) and unpacked at boot by the kernel's own inflate (`kernel/inflate.c`, `kernel/wallpaper.c`), with a checksum: a damaged image falls back to the plain colour (tested). ~65 ms in QEMU; **measure on the EeePC**. The dock and highlights are real translucent blends over it
  - **Home screen:** just the wallpaper and a **dock** at the bottom with the apps and **Show Apps**. The dock is hidden while an app is open (apps get everything below the top bar)
  - **App menu:** Show Apps opens a full-screen grid of apps (48×48 PNG icons, `docs/ASSET-PROMPTS.md` §4) over the dimmed background; Esc, empty space or Show Apps closes it
  - Tests: dock, app menu, Home, restart, and the clock (QEMU's clock set to 23:59:57 on 31 Dec, rolling over to Fri 1 Jan)

## 4. Core GUI widgets (~5 hrs)
- [x] Button, label, basic layout container — minimum set, not a full toolkit — `kernel/widget.c` (2026-09-30), Adwaita dark: label, button (normal, suggested, destructive, flat), boxed list (icons, detail, selection, double-click, keyboard, scrolling), text entry (cursor, editing keys, scrolling) and alert dialog (dims what's behind, Enter/Esc). Layout is done by the app; no container toolkit was needed. Depends only on gfx, the font and the theme, so it can move into the app library in §5
- [x] Each widget: state (hover/pressed/disabled), not just static appearance — hover, pressed, disabled, focused, selected; input sets a `dirty` flag and only those widgets are redrawn. The window system now passes every pointer update (`WM_EVENT_POINTER`, moves merged) with a timestamp. 39-check self-test (`tests/kernel/selftest_widgets.c`)
- [x] Keep this minimal — Phase 3 is where these get polished, not here — no anti-aliasing, no animations, no blinking cursor (no timer yet). **Files** uses them all on an in-memory demo folder: create file/folder, rename, delete through dialogs, with FAT32's name rules; Ctrl+N, F2, Delete. End-to-end test drives it from the keyboard

## 5a. Storage for the Files app (~8 hrs, added 2026-09-30)
Disks came off the Non-Goals list for the first app, Files (see `docs/NON-GOALS.md`, Decided).
- [x] Block device interface through `driver_t` (read/write sectors by LBA via `ioctl`) — `kernel/block.h`: `BLK_GET_INFO`, `BLK_READ`, `BLK_WRITE`
- [ ] **Boot USB stick:** reached through the BIOS with a real-mode call per request (port v1's `boot/realmode_thunk.asm` + `drivers/bios_disk.c` deliberately), read and write — done and tested in QEMU (`boot/bios_thunk.asm`, a generic INT 13h caller; `drivers/bios_disk.c`, device `boot0`; a write is checked in the image file afterwards). **Needs an EeePC run** to tick: the BIOS's USB support is the real target
- [ ] **Internal disk:** ATA PIO driver for the ICH7's IDE-compatible SATA (`8086:27c4`), read-only — done and tested in QEMU (`drivers/ata.c`, device `ata0`: polled PIO, LBA28/48, legacy or native ports; the drive we booted from is recognised and skipped; `BLK_WRITE` is always `-EROFS`). The test attaches a stand-in disk and checks it's byte-for-byte unchanged afterwards. **Needs an EeePC run** to tick. Writing to a FAT32 internal partition (allowed by `docs/NON-GOALS.md`) isn't done: the driver stays read-only until asked for
- [x] MBR partition table; `make usb` gives the stick a second partition, FAT32, that Windows can open too — stage 1's partition 2: 64 MiB FAT32 from 1 MiB, made by `mkfs.fat` and filled with `mcopy` from `assets/disk/` (normal image only). `kernel/storage.c` reads each disk's table; FAT32 partitions are mounted, others listed as unsupported
- [x] FAT32: list, read, create, delete, rename files and folders. Writes only ever go to FAT32 partitions, and are checked by a test that compares the result with Linux's own FAT tools — `kernel/fat32.c`, with VFAT long names. `tests/fat/` builds it on Linux (with AddressSanitizer) and runs 26 checks on the real partition image, then `fsck.fat` must find it clean and mtools must list the same tree. The kernel test drives Files on the partition in QEMU and checks the image with `fsck.fat` and mtools too
- [x] An NTFS (or any non-FAT32) internal disk shows as "not supported" and is never written — Files' Places lists it as "Not supported: NTFS or exFAT" and won't open it; nothing but the partition table is ever read from it (tested)

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

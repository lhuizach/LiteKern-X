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
- [ ] Confirm no tearing/lag at this stage — this was already a v1 strength, don't lose it — needs the EeePC (`litekernx.img`: move the touchpad over the log)

## 3. Window/surface model (~4 hrs)
- [ ] Decide: single fullscreen surface for now, or a real windowing model? (Given hardware constraints, simple is fine — don't over-scope)
- [ ] Basic input routing — keyboard/mouse events reach the right surface
- [ ] Redraw/damage handling so you're not redrawing the whole screen every frame (this matters a lot on Atom-class hardware)

## 4. Core GUI widgets (~5 hrs)
- [ ] Button, label, basic layout container — minimum set, not a full toolkit
- [ ] Each widget: state (hover/pressed/disabled), not just static appearance
- [ ] Keep this minimal — Phase 3 is where these get polished, not here

## 5. KERN86 app integration (~3 hrs)
- [ ] Port `.lkx` loader, `kerns.json` manifest parsing, `kern86.h` API from v1
- [ ] Apps run in ring 3 — `kern86.h` becomes a thin user-side wrapper over the Phase 1 syscall gate (`int 0x80`), and the kernel side calls through the driver interface instead of v1's direct hardware access
- [ ] Extend the syscall table only as the test app needs it (draw/present surface, poll input, exit) — no speculative syscalls
- [ ] Get ONE minimal test app running end-to-end through the new GUI (doesn't need to be useful — proves load → manifest → API → render → input works)
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

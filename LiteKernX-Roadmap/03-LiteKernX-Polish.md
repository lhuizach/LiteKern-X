# LiteKern X — Phase 3: Polish
**Budget: ~15 hrs** (was ~10; +5 for §5 Customisation, added 2026-09-29)
**Prerequisite: Phase 2 fully done (see 02-LiteKernX-GUI.md Done Criteria)** — started 2026-10-01 by the user's decision, with Phase 2's EeePC runs still open (see the note under Phase 2's Done Criteria)

Goal: take the working-but-plain GUI/apps from Phase 2 and make them feel finished. This is deliberately last — polishing something unstable just means redoing the polish later.

---

## 1. Visual polish (~3 hrs)
- [x] Consistent spacing/margins across widgets — the theme's spacing tokens (margin 12, spacing 6, radius 6/12, row 40, header 46) everywhere; corners and circles are now anti-aliased (4x4-sampled coverage, `kernel/gfx.c`); text is the proportional anti-aliased GUI font (§5)
- [x] Color scheme finalized (even a simple one) — apply consistently rather than per-screen. Every widget reads its colours from the theme (§5), never hard-coded values — Adwaita dark and light (`kernel/theme.c`); widgets shade with the theme's `ink`. Deliberate exceptions: the shell (top bar, its menu, the dock) stays dark in both styles like GNOME Shell, and the splash is black
- [ ] Icon/cursor set finalized: the PNG cursors and SVG icons in `assets/` (rules in `docs/ASSET-PROMPTS.md`) — the set is in place and themed nowhere: the user's arrow cursor, the logo, and 48x48 PNG app icons (icons became PNG in Phase 2). Files, Log, Settings and Calculator still use the generated placeholders (`tools/icon-placeholders.py`): **the user's own icons replace them** (docs/ASSET-PROMPTS.md §4)

## 2. Animations (~2.5 hrs)
- [x] Window/app open-close transitions (keep them short — this is a low-power Atom CPU, don't tank performance for polish) — the window grows out of the clicked icon and shrinks back into its dock icon: 8 frames, 140 ms, eased (`kernel/desktop.c`)
- [x] Button/widget feedback (press states, hover if input supports it) — hover and pressed on every button, list row, dock item, app-menu tile and top-bar button; disabled and focused states
- [ ] Measure frame cost of animations — if they hurt responsiveness, cut them, don't just accept it — measured and logged on every run (`anim: ... slowest frame N ms`; QEMU 2-5 ms); a frame over 30 ms skips the rest automatically. **Needs the EeePC numbers**

## 3. App polish (~3 hrs)
- [x] Revisit the one test app from Phase 2 — make it a real, presentable minimal app — Files: Places, folders, create/rename/delete with dialogs, a text viewer (up to 64 KB), keyboard shortcuts, follows theme changes
- [x] Add more small KERN86 apps if time allows, each fully finished (this is the anti-v1 rule: finished small beats unfinished many). There's no fixed cap any more (the 3-app limit was removed 2026-09-29), but don't start a new app until the previous one is done — **Settings** (Appearance) and **Calculator** (exact fixed-point decimals, errors for overflow and / 0), each tested
- [x] Consistent app chrome/UI conventions across apps — every app: the header bar (title, back button where it applies, close), content on window_bg with the same margins, the shared widgets, Esc/Backspace to go back, K86_EVENT_THEME to redraw

## 4. Final stability + performance pass (~1.5 hrs)
- [ ] Re-check boot time against `docs/BOOT-BUDGET.md`: ≤ 5 s bootloader entry → desktop, hard ceiling 10 s, BIOS POST excluded, with visible progress the whole way (the budget was ≤ 1000 ms until 2026-09-29) — QEMU: `[boot] desktop t=376` (ms), checked by `make test` on every run (fails over 5000). **The EeePC number is the one that counts** (`docs/HARDWARE-TEST.md` §F)
- [x] Full run-through: boot → GUI → open apps → close apps → shutdown, no crashes — automated in `tests/kernel/test-kernel.sh` (every dock app opened and closed, then the power menu). It ends in **Restart**: power-off needs ACPI, which stays in Phase 5 §1 (it starts with a design decision), so Shut Down is shown greyed out
- [x] Fix anything found — do not carry known bugs into Phase 4 — found and fixed during Phase 3: the top bar invisible in the light style, an unowned window not closed with its app, the animation buffer allocated mid-run; no known bugs open in QEMU

## 5. Customisation, GNOME-style (~5 hrs, added 2026-09-29)
A few well-chosen options, like GNOME/libadwaita, not KDE-style "configure everything". Do §1–§3 first so there's a finished look to vary.
- [x] Theme tokens: `struct theme` (`window_bg`, `view_bg`, `headerbar_bg`, `fg`, `fg_dim`, `accent_bg`, `accent_fg`, `border`, `warning`, `destructive`, plus spacing) behind `theme_get()`; no widget or app hard-codes a colour — apps get their copy with SYS_THEME_GET
- [x] Style: Dark (navy) and Light (mist) themes, both built from the `docs/ASSET-PROMPTS.md` palette — superseded by the Adwaita decision (2026-09-30): libadwaita's dark and light palettes
- [x] Accent colour: 5–6 presets (sky default); changing it only swaps `accent_bg`/`accent_fg` — GNOME's: blue (default), teal, green, orange, red, purple
- [x] Wallpaper: a solid colour, plus 3–5 compressed images; measure the `assets` boot phase on the EeePC (`docs/BOOT-BUDGET.md`) — first one made (2026-09-30): `assets/wallpapers/crossing.png` + `crossing-light.png` (1024×600, 155 KB / 96 KB, pass `docs/ASSET-PROMPTS.md` §8). **Showing one wallpaper came forward into Phase 2** (the shell, 2026-09-30: packed into the kernel, unpacked at boot). Done here: the ramdisk moved onto the disk (read lazily: only its index at boot), so any number fit; four wallpapers, each with a light version and a build-time thumbnail: crossing (the user's), dusk, aurora, drift (`tools/make-wallpapers-more.py`), plus none. **The `assets` boot phase still needs measuring on the EeePC**
- [x] Boot splash: the logo (`assets/logo.png`, made 2026-09-30) with a progress bar, or the scrolling boot log; either way, visible progress the whole boot — `make SPLASH=logo` (default) or `SPLASH=log`; the bar moves at each boot step
- [x] Proportional anti-aliased font like v1's, replacing the 8×16 bitmap font in the GUI (the console keeps the bitmap font) — Ubuntu Sans in five styles, rasterised once into a committed atlas (`assets/fonts/`, licence noted there)
- [x] Applying a change is one `screen_damage_all()` + present, not a reboot
- [x] Settings UI: an "Appearance" page (style preview, accent dots, wallpaper grid), in the shell or as a KERN86 app; add theme syscalls only when that app needs them — `apps/settings`, with SYS_THEME_GET, SYS_APPEARANCE(_SET), SYS_WALLPAPER_THUMB
- [x] Build-time defaults (e.g. `make THEME=light ACCENT=teal`), since settings aren't saved across reboots until Phase 5 §3 — `make STYLE=light ACCENT=teal WALLPAPER=dusk SPLASH=log`
- [x] Cursors stay black and white and icons stay fixed; neither is themed

## 6. Windows, Windows-style (added 2026-10-02, the user's request)
Apps leave full screen for floating windows, and several run at once. Decided with the user: Files and Log are the favourites (pinned in the dock); any other open app joins the dock after a line; while a maximised window is in front the dock hides and the bottom edge brings it back (it stays while the pointer is on it); the focused app's dot is a long pill. One title bar per window: the app's buttons on the left, its name in the middle, minimise / maximise / close on the right.
- [x] Several apps at once: one address space each (`kernel/vmm.c`); an app waiting for input gives the CPU back and is resumed when its window has an event (`kernel/user.c`, `kernel/lkx.c`): one trap stack serves them all
- [x] Floating windows (`kernel/wm.c`): drag by the header bar, resize from edges and corners (minimum sizes), maximise (button, double-click, or drag to the top), snap to half the screen (drag to a side), minimise, click to bring to the front; keys go to the focused window. Apps get `K86_EVENT_RESIZE`
- [x] Each window keeps its own pixels (mapped into its app as the canvas), so covering and uncovering never asks the app to redraw; the shell composites wallpaper, windows (shadows, rounded corners), dock, top bar and menus, only where something changed
- [x] Dock: favourites, a line, the other open apps, Show Apps; dots: grey (minimised), white (open), a pill (focused); everything moves on springs (icons sliding and popping in and out, dots morphing, the dock rising)
- [x] Window animations: open from the icon, minimise into the dock and back, maximise, close (fade); timed, and cut short on a slow machine like the others
- [x] Log is a window like the rest (the console draws into it)
- [ ] Measure dragging and the animations on the EeePC (`anim:` lines; a drag redraws two window-sized areas per move)

---

## Phase 3 Done Criteria
Status 2026-10-01: everything QEMU can show is done. Left: the EeePC numbers (boot time, animation frame cost, the `assets` phase) and the user's own app icons.
- [ ] GUI and apps look and feel consistent, not just functional
- [ ] Boot time still meets budget
- [ ] No known bugs or unfinished visual states remain
- [x] At least 1-2 fully polished KERN86 apps exist — Files, Calculator, Settings
- [x] Style, accent, wallpaper and splash can each be changed, and every screen follows the theme — style, accent and wallpaper in Settings; all four, splash included, at build time

**Do not start Phase 4 (Publishing) until every box above is checked.**

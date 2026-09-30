# LiteKern X — Phase 3: Polish
**Budget: ~15 hrs** (was ~10; +5 for §5 Customisation, added 2026-09-29)
**Prerequisite: Phase 2 fully done (see 02-LiteKernX-GUI.md Done Criteria)**

Goal: take the working-but-plain GUI/apps from Phase 2 and make them feel finished. This is deliberately last — polishing something unstable just means redoing the polish later.

---

## 1. Visual polish (~3 hrs)
- [ ] Consistent spacing/margins across widgets
- [ ] Color scheme finalized (even a simple one) — apply consistently rather than per-screen. Every widget reads its colours from the theme (§5), never hard-coded values
- [ ] Icon/cursor set finalized: the PNG cursors and SVG icons in `assets/` (rules in `docs/ASSET-PROMPTS.md`)

## 2. Animations (~2.5 hrs)
- [ ] Window/app open-close transitions (keep them short — this is a low-power Atom CPU, don't tank performance for polish)
- [ ] Button/widget feedback (press states, hover if input supports it)
- [ ] Measure frame cost of animations — if they hurt responsiveness, cut them, don't just accept it

## 3. App polish (~3 hrs)
- [ ] Revisit the one test app from Phase 2 — make it a real, presentable minimal app
- [ ] Add more small KERN86 apps if time allows, each fully finished (this is the anti-v1 rule: finished small beats unfinished many). There's no fixed cap any more (the 3-app limit was removed 2026-09-29), but don't start a new app until the previous one is done
- [ ] Consistent app chrome/UI conventions across apps

## 4. Final stability + performance pass (~1.5 hrs)
- [ ] Re-check boot time against `docs/BOOT-BUDGET.md`: ≤ 5 s bootloader entry → desktop, hard ceiling 10 s, BIOS POST excluded, with visible progress the whole way (the budget was ≤ 1000 ms until 2026-09-29)
- [ ] Full run-through: boot → GUI → open apps → close apps → shutdown, no crashes
- [ ] Fix anything found — do not carry known bugs into Phase 4

## 5. Customisation, GNOME-style (~5 hrs, added 2026-09-29)
A few well-chosen options, like GNOME/libadwaita, not KDE-style "configure everything". Do §1–§3 first so there's a finished look to vary.
- [ ] Theme tokens: `struct theme` (`window_bg`, `view_bg`, `headerbar_bg`, `fg`, `fg_dim`, `accent_bg`, `accent_fg`, `border`, `warning`, `destructive`, plus spacing) behind `theme_get()`; no widget or app hard-codes a colour
- [ ] Style: Dark (navy) and Light (mist) themes, both built from the `docs/ASSET-PROMPTS.md` palette
- [ ] Accent colour: 5–6 presets (sky default); changing it only swaps `accent_bg`/`accent_fg`
- [ ] Wallpaper: a solid colour, plus 3–5 compressed images; measure the `assets` boot phase on the EeePC (`docs/BOOT-BUDGET.md`) — first one made (2026-09-30): `assets/wallpapers/crossing.png` + `crossing-light.png` (1024×600, 155 KB / 96 KB, pass `docs/ASSET-PROMPTS.md` §8)
- [ ] Boot splash: the logo (`assets/logo.png`, made 2026-09-30) with a progress bar, or the scrolling boot log; either way, visible progress the whole boot
- [ ] Proportional anti-aliased font like v1's, replacing the 8×16 bitmap font in the GUI (the console keeps the bitmap font)
- [ ] Applying a change is one `screen_damage_all()` + present, not a reboot
- [ ] Settings UI: an "Appearance" page (style preview, accent dots, wallpaper grid), in the shell or as a KERN86 app; add theme syscalls only when that app needs them
- [ ] Build-time defaults (e.g. `make THEME=light ACCENT=teal`), since settings aren't saved across reboots until Phase 5 §3
- [ ] Cursors stay black and white and icons stay fixed; neither is themed

---

## Phase 3 Done Criteria
- [ ] GUI and apps look and feel consistent, not just functional
- [ ] Boot time still meets budget
- [ ] No known bugs or unfinished visual states remain
- [ ] At least 1-2 fully polished KERN86 apps exist
- [ ] Style, accent, wallpaper and splash can each be changed, and every screen follows the theme

**Do not start Phase 4 (Publishing) until every box above is checked.**

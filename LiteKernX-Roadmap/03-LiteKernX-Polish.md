# LiteKern X — Phase 3: Polish
**Budget: ~10 hrs**
**Prerequisite: Phase 2 fully done (see 02-LiteKernX-GUI.md Done Criteria)**

Goal: take the working-but-plain GUI/apps from Phase 2 and make them feel finished. This is deliberately last — polishing something unstable just means redoing the polish later.

---

## 1. Visual polish (~3 hrs)
- [ ] Consistent spacing/margins across widgets
- [ ] Color scheme finalized (even a simple one) — apply consistently rather than per-screen
- [ ] Icon/cursor set finalized using the SVG cursor spec from Phase 2

## 2. Animations (~2.5 hrs)
- [ ] Window/app open-close transitions (keep them short — this is a low-power Atom CPU, don't tank performance for polish)
- [ ] Button/widget feedback (press states, hover if input supports it)
- [ ] Measure frame cost of animations — if they hurt responsiveness, cut them, don't just accept it

## 3. App polish (~3 hrs)
- [ ] Revisit the one test app from Phase 2 — make it a real, presentable minimal app
- [ ] Add 1-2 more small KERN86 apps if time allows, each fully finished (this is the anti-v1 rule: finished small beats unfinished many)
- [ ] Consistent app chrome/UI conventions across apps

## 4. Final stability + performance pass (~1.5 hrs)
- [ ] Re-check boot time against `docs/BOOT-BUDGET.md`: ≤ 5 s bootloader entry → desktop, hard ceiling 10 s, BIOS POST excluded, with visible progress the whole way (the budget was ≤ 1000 ms until 2026-09-29)
- [ ] Full run-through: boot → GUI → open apps → close apps → shutdown, no crashes
- [ ] Fix anything found — do not carry known bugs into Phase 4

---

## Phase 3 Done Criteria
- [ ] GUI and apps look and feel consistent, not just functional
- [ ] Boot time still meets budget
- [ ] No known bugs or unfinished visual states remain
- [ ] At least 1-2 fully polished KERN86 apps exist

**Do not start Phase 4 (Publishing) until every box above is checked.**

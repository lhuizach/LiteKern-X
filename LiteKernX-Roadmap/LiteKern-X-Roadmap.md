# LiteKern X — Rebuild Roadmap

> **⚠ Superseded for execution details.** This file keeps the *why*, target hardware, and The One Rule. The actual work plan and Done Criteria live in the phase files, which are the source of truth:
> 1. `01-LiteKernX-Base.md` — kernel, PCI enumeration, driver layer, paging/rings/syscalls, core drivers
> 2. `02-LiteKernX-GUI.md` — rendering, cursor, surfaces, widgets, KERN86 apps
> 3. `03-LiteKernX-Polish.md`
> 4. `04-LiteKernX-Publishing.md`
> 5. `05-LiteKernX-Expansion.md` — after v1.0: ACPI, audio, USB, networking, users, localisation, x86-64
>
> The Stage 0–6 breakdown below is kept for history. Where it disagrees with the phase files (e.g. the `driver_t` definition, KERN86 timing, memory protection), the phase files win.

## Why X exists
LiteKern v1 (~50+ hrs of work) proved the concept — a from-scratch OS running on the EeePC 1000HE (Intel Atom N270, 32-bit x86) with a working KERN86 app architecture (`.lkx` format, `kerns.json` manifest, `kern86.h` API), framebuffer rendering, and cursor rasterization. But it grew organically: features were started faster than they were finished, drivers were bolted on ad hoc, and boot time ballooned to ~5000ms.

**X is not a port. It's a clean-slate rewrite** that keeps the lessons and the KERN86 app model, but enforces one rule the whole way through:

> **A subsystem is not "done" until it meets its Done Criteria below. No new subsystem starts until the current one is done.**

This is the single biggest behavioral change from v1 to X.

---

## Target hardware
- **Primary:** ASUS EeePC 1000HE — Intel Atom N270, 32-bit x86 (same as v1, for now)
- **Future:** Raspberry Pi 4 (4GB), ARM64 — will require a 64-bit rewrite of the kernel core. Not in scope for X v1.0; noted here so early design decisions (e.g. avoiding x86-only assumptions where cheap to do so) can keep this door open without slowing X down.

---

## Stage 0 — Project Discipline Setup
Before any code:
- [ ] New repo (or clean branch) for X. Do not import v1 code wholesale — port logic deliberately, piece by piece, only when a stage needs it.
- [ ] Write a one-page **Non-Goals** list: features from v1 you are explicitly NOT building yet (e.g. GUI polish, extra drivers, extra apps). Revisit only after MVP boots stable.
- [ ] Set up a boot-time budget: **target boot ≤ 1000ms** (down from v1's ~5000ms). Every stage below should be built with this budget in mind, not fixed at the end. *(Changed 2026-09-29: now ≤ 5 s to the desktop with visible progress, hard ceiling 10 s — see `docs/BOOT-BUDGET.md`.)*

**Done when:** repo exists, Non-Goals list is written, boot budget is documented.

---

## Stage 1 — Minimal Boot Path
Goal: the leanest possible path from power-on to kernel entry, instrumented so you can *measure* where time goes from the start (this is what v1 skipped — boot perf was tackled last, blind).

- [ ] Bootloader stage — no unnecessary hardware probing, no delays "just in case"
- [ ] Kernel entry + minimal init (stack, GDT/IDT if x86, basic memory map read)
- [ ] Add **boot-stage timestamps** (even crude ones — a counter or timer read at each major phase) so every later stage's cost is visible immediately, not discovered at the end
- [ ] Confirm you can measure: bootloader time, kernel init time, time-to-first-driver-call

**Done when:** kernel reaches a "ready for drivers" state with per-phase timing printed/logged, and the empty-shell boot time is known and recorded.

---

## Stage 2 — Driver Abstraction Layer
Goal: fix the "broken drivers" problem structurally, not by trying harder next time.

- [ ] Define the driver interface (minimal, fixed):
  ```c
  typedef struct {
      int  (*init)(void);
      int  (*read)(void *buf, size_t len);
      int  (*write)(const void *buf, size_t len);
      void (*shutdown)(void);
  } driver_t;
  ```
- [ ] Driver registry — kernel only ever calls through the interface, never touches a driver's internals directly
- [ ] Registration must **fail loudly** if a driver doesn't correctly implement `init()` — no silent partial-driver usage
- [ ] Write ONE trivial reference driver against this interface first (e.g. a null/debug driver) to prove the abstraction works before porting anything real

**Done when:** the interface is defined, the registry works, and one working reference driver is registered and callable through it — not just compiling, actually exercised.

---

## Stage 3 — Port Core Drivers (one at a time, each to completion)
Only start the next driver once the current one is done. Suggested order (adjust to what X actually needs first):

1. [ ] **Display/framebuffer driver** — port from v1's rendering work, but through the new interface
2. [ ] **Keyboard driver**
3. [ ] **Disk/storage driver** (if X needs persistent storage at this stage)

For each driver:
- [ ] Implements the full `driver_t` interface correctly
- [ ] Tested in isolation (stub/mock the rest of the system if needed)
- [ ] No known broken states — if it can't do something yet, it says so (returns an error), it doesn't silently misbehave

**Done when:** all core drivers for MVP are registered, pass isolated tests, and boot-stage timing shows their init cost.

---

## Stage 4 — Rendering & Cursor (carry forward from v1)
This is the part of v1 that was already in good shape — port deliberately, don't rewrite from scratch unless the driver-interface change forces it.

- [ ] Framebuffer rendering pipeline ported to sit behind the display driver interface
- [ ] Cursor rasterization (SVG-spec cursors) ported and verified against the same spec used in v1
- [ ] Basic performance check — this was a recent v1 focus, so confirm X doesn't regress it

**Done when:** rendering + cursor work at parity with v1's best state, running through the new driver layer.

---

## Stage 5 — KERN86 App Layer (kept from v1)
- [ ] Port `.lkx` format loader
- [ ] Port `kerns.json` manifest parsing
- [ ] Port `kern86.h` API, adjusted if needed to call through the new driver interface instead of old direct hardware access
- [ ] Get ONE minimal test app running end-to-end (doesn't need to be useful — just needs to prove load → manifest → API → run works)

**Done when:** one real `.lkx` app runs successfully via the new stack.

---

## Stage 6 — MVP Boot Test
- [ ] Full boot: bootloader → kernel → drivers → rendering → one KERN86 app running
- [ ] Boot time measured against the budget in `docs/BOOT-BUDGET.md` (≤ 5 s to the desktop, ceiling 10 s)
- [ ] No known broken/half-finished features present at all — if it's not done, it's not in this build

**Done when:** X boots clean, boots fast (or you know exactly why not, with data from your Stage 1 instrumentation), and does only what it currently claims to do.

---

## After MVP (explicitly out of scope until Stage 6 is done)
- Landing page / OS UI mockup work (already exists from v1 — revisit, don't rebuild blind)
- Additional drivers
- ARM64 / Raspberry Pi 4 port
- Samsung Galaxy S10e port
- Any feature from your Non-Goals list

---

## The One Rule
If you're about to start something not in the current stage: stop, and either (a) finish the current stage first, or (b) consciously add it to Non-Goals so it's a deliberate decision, not drift. This is the exact habit that produced v1's unfinished-feature problem — X's whole point is breaking it.

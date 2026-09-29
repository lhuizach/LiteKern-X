# LiteKern X — Phase 4: Publishing
**Prerequisite: Phase 3 fully done (see 03-LiteKernX-Polish.md Done Criteria)**

Goal: make LiteKern X something other people can actually download, boot, and try.

---

## 1. Build a bootable ISO
- [ ] Package the custom bootloader + kernel as a raw disk image (`.img`) that can be written straight to a USB stick — the EeePC has no optical drive and boots from USB
- [ ] Optional: also produce a hybrid `.iso` (El Torito no-emulation boot of the same stage 2, via `xorriso`) for people testing in VMs
- [ ] Test the ISO in a VM (QEMU/VirtualBox) before testing on real hardware — faster iteration, safer
- [ ] Test the ISO on the actual EeePC 1000HE target hardware
- [ ] Document exact build steps so the ISO is reproducible, not a one-off

## 2. Versioning
- [ ] Decide a version scheme (e.g. `LiteKern X v0.1.0`) 
- [ ] Tag the release in your repo
- [ ] Write a CHANGELOG summarizing what's actually in this release (be honest about what's NOT included — avoids v1's "features that don't really work" trap)

## 3. Website
- [ ] Reuse/adapt the landing page from v1's design phase rather than starting from scratch
- [ ] Core content needed: what LiteKern X is, screenshots/gif of the GUI, download link for the ISO, basic "how to boot this" instructions
- [ ] Keep initial scope small — a single clean page beats a half-finished multi-page site (same discipline as the rest of this project)

## 4. Documentation
- [ ] Minimal README: what it is, how to build from source, how to boot the ISO
- [ ] KERN86 app development notes — if you want others (or future you) to write `.lkx` apps, document the manifest format and `kern86.h` API
- [ ] Known limitations section — be upfront about what's not supported yet (target hardware only, no ARM/Pi yet, etc.)

## 5. Distribution
- [ ] Host the ISO somewhere stable (GitHub Releases is the easy option if the repo is there)
- [ ] Link it from the website
- [ ] Optional: post to relevant communities (r/osdev, OSDev forums) for feedback — useful both for validation and bug reports from real hardware you don't own

---

## Phase 4 Done Criteria
- [ ] A working ISO exists, versioned and tested on real hardware
- [ ] A live website exists with download + basic docs
- [ ] Someone unfamiliar with the project could plausibly download and boot it from the website alone

---

## Future work (explicitly out of scope for X v1.0)
- Phase 5 (`05-LiteKernX-Expansion.md`): ACPI power management, audio, USB, networking, multiple users, localisation, x86-64
- ARM64 / Raspberry Pi 4 port
- Samsung Galaxy S10e port
- Anything still on `docs/NON-GOALS.md`

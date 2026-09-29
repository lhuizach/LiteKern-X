# LiteKern X — Testing on the EeePC 1000HE

Phase 1's code is done and tested in QEMU and VirtualBox. The roadmap's Done Criteria need it confirmed on the real machine before Phase 2 starts. This is the checklist for that run, and it takes about 20 minutes.

You need: the EeePC 1000HE, a USB stick **whose contents you don't mind losing**, and a phone camera.

## 1. Build the images
From PowerShell in the project folder:
```
wsl make usb
```
This creates three 1 MiB images in `build\usb\`:
| Image | What it's for |
|---|---|
| `litekernx.img` | The normal boot: bootloader, kernel, drivers, on-screen log, keyboard and touchpad |
| `litekernx-ring3-test.img` | The same kernel plus the ring 3 self-test (12 checks of paging, syscalls and fault isolation) |
| `litekernx-vbios-diag.img` | The same kernel plus a read-only dump of the video BIOS (§4C) |

## 2. Write an image to the USB stick
Use [Rufus](https://rufus.ie) (Windows, portable) or [balenaEtcher](https://etcher.balena.io):
- **Rufus:** select the stick, click SELECT and pick `litekernx.img`, then START. If Rufus asks, choose **DD image mode**.
- **Etcher:** Flash from file, select the target, then Flash.

Writing an image **erases the whole stick**. Double-check you've selected the USB stick and not another drive.

## 3. Boot the EeePC from it
1. Plug the stick in, power on, and tap **Esc** during the ASUS logo. A boot menu appears. Pick the USB stick.
2. If Esc does nothing: press **F2** at power-on to enter the BIOS, disable **Boot Booster** (it skips the key checks), save, and try again. If the stick isn't in the menu, check the BIOS boot device list.

## 4. What to check and photograph

### A. Normal boot (`litekernx.img`)
| # | Check | Expected | Roadmap box |
|---|---|---|---|
| 1 | Screen after boot | **Navy background with the log**. Red means a panic (photograph it). Black means it didn't reach the kernel: look for an `LKX stage1:` / `LKX stage2:` message. | Phase 1 Done: boots to ready |
| 2 | Press **Home** (Fn+←), photograph, then **PgDn** (Fn+↓) and photograph until the end | The whole log, top to bottom | — |
| 3 | `fb ...` line | `fb 1024x600x32 ...`, which means stage 2 found the panel's native mode | §2 VBE mode |
| 4 | `[boot]` lines | The per-phase times and `ready t=...`. These are the **first real boot-time numbers** against the 1000 ms budget. | §2, boot budget |
| 5 | `pci ...` lines | Intel `8086:27xx` devices (945GSE/ICH7) and the Atheros Ethernet. This is the real device list. | §3 |
| 6 | `dev ...` lines | `fb0`, `kbd0` and `mouse0` **bound**. `com1 ... FAILED (ENODEV)` is **correct**: the EeePC has no serial port. | §4, §6 |
| 7 | Type some letters, Shift+letters and Caps Lock | A `kbd: key ...` line per key | §6.2 |
| 8 | Move on the touchpad and press both buttons | `mouse: (x, y) buttons L--` / `--R` lines. Tapping may not click in this basic mode, so use the physical buttons. | §6.3 |
| 9 | Optional: time from power-on to the ASUS logo disappearing | A stopwatch reading of BIOS POST, just for reference. It's excluded from the budget. | Boot budget |

### B. Ring 3 self-test (`litekernx-ring3-test.img`)
Write this image to the stick and boot it the same way.
| Check | Expected | Roadmap box |
|---|---|---|
| Screen colour | **Navy**, with `selftest: user 12/12 passed` near the end. Red means a check failed: photograph it. | Phase 1 Done: paging + rings, ring 3 syscall + bad pointer |

### C. Video BIOS diagnostic (`litekernx-vbios-diag.img`), for the 1024×600 fix
The EeePC's video BIOS has no 1024×600 mode, so X currently runs at a stretched 800×600. This image changes nothing. It only reads and prints what the fix (patching the BIOS mode table, like `915resolution`) needs.

Write it to the stick, boot it, then press **End** and **PgUp** until you reach the line `diag: vbe modes`. Photograph from there down to `kbd: ready`, making sure every line is sharp and readable. It's about 30–40 lines, so it takes 2 photos.
| Line(s) | What it tells me |
|---|---|
| `vbe: VBE 3.0, '...'` | The video BIOS's name and version, and which 32 bpp modes it offers |
| `diag: vbe modes ...` | Every mode, as mode number, size, colour depth, type and flags |
| `diag: host ... pam 90-96 = ...` | Whether the BIOS copy in RAM can be written (the patch needs that) |
| `diag: mode table at +....` and the `res +....` lines | The Intel table to patch, and the timing record for each resolution |

### D. The 1024×600 fix (`litekernx.img`, rebuilt after the diagnostic)
Boot the normal image again. Pass means:
| Line | Expected |
|---|---|
| `vbe: patched the Intel video BIOS: mode 0x5c is now 1024x600 (panel native)` | Present |
| `fb ...` | `fb 1024x600x32 ...` |
| `console: ...` | `128x37 characters`. The text should look sharp and **not stretched**. |
| Screen | Navy, with the text filling the whole panel width |

If you instead see `vbe: WARNING: could not patch ...`, the shadow RAM stayed locked: photograph the log. If the screen stays black, write `litekernx-vbios-diag.img` again and photograph the `diag:` lines, which now show the patched record too.

## 5. Send back
The photos (or just the answers to the table). I'll tick the roadmap boxes, fill in the measurements table in `docs/BOOT-BUDGET.md`, and fix anything the real hardware disagrees with before Phase 2.

## If something goes wrong
| Symptom | Likely cause |
|---|---|
| The stick isn't offered as a boot device | The image wasn't written in raw/DD mode, or Boot Booster is on |
| `LKX stage1: no LBA` | The BIOS is emulating the stick as a floppy. Try another stick. |
| `LKX stage1: disk read error` / `LKX stage2: kernel read error` | A flaky stick or write. Rewrite it, or try another stick. |
| `LKX stage2: no usable VBE mode (need 32 bpp LFB)` | The GMA 950's video BIOS offers no 32 bpp mode, so stage 2 needs a 24 bpp fallback. Photograph it. |
| Plain navy or red screen with no text | `console: unavailable`: the video BIOS didn't give a font |
| Keyboard or touchpad lines never appear | Check the `dev kbd0` / `dev mouse0` line for `FAILED (...)` |

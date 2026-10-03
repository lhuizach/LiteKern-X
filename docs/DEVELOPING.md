# Developing LiteKern X

Everything you need to build, test and work on LiteKern X. For what the project is, see the [README](../README.md); for the plan, see [`LiteKernX-Roadmap/`](../LiteKernX-Roadmap/) (the phase files there are the source of truth).

## Setup (Windows + WSL Ubuntu)
Run these once, from PowerShell in the project folder:
```
wsl sudo bash tools/setup-wsl.sh
wsl make check-tools
```
The toolchain is the host's `gcc -m32 -ffreestanding`, GNU `ld` and `nasm`, all in WSL. A proper `i686-elf` cross-compiler is a [Non-Goal](NON-GOALS.md) until host gcc causes real problems.

## Everyday commands
| Command | What it does |
|---|---|
| `wsl make` | Builds `build/litekernx.img`: bootloader, kernel, apps, wallpapers, and a FAT32 partition |
| `wsl make run` | Builds and boots it in a QEMU window |
| `wsl make test` | Runs every test: the VM smoke test, the bootloader tests, the FAT32 tests and the kernel tests (about 10 minutes) |
| `wsl make usb` | Builds the images for the real Eee PC into `build/usb/` (see [HARDWARE-TEST.md](HARDWARE-TEST.md)) |
| `wsl make debug` | Like `run`, paused, with a gdb stub on `:1234`; interrupts and resets logged to `build/qemu-debug.log` |
| `wsl make smoke` / `smoke-gui` | Boots a tiny test image, to check the VM itself works |

Build-time choices (settings aren't saved across restarts yet):
```
wsl make STYLE=light ACCENT=teal WALLPAPER=dusk SPLASH=log
```
The screen saver (glowing ribbons, `kernel/screensaver.h`) starts after 5 minutes without input; `SCREENSAVER=<seconds>` changes that, and `SCREENSAVER=0` turns it off. To see it straight away: `wsl make run SCREENSAVER=5`.

## VirtualBox
There's a second dev VM, `LiteKern X`, in your Windows VirtualBox, with the same Eee PC-like profile as the QEMU VM plus the real panel's **1024×600** mode (QEMU can't do that one). It's driven from WSL by [`vm/vbox.sh`](../vm/vbox.sh):

| Command | What it does |
|---|---|
| `wsl make vbox-create` | Creates and registers the VM (once) |
| `wsl make vbox` | Rebuilds, copies the image to the VM's disk, and boots it in a window. Power the VM off first |
| `wsl make vbox-test` | The same, headless: waits for the desktop, saves `build/vbox/screen.png`, powers off |
| `wsl bash vm/vbox.sh stop\|status\|destroy` | Manage the VM |

The VM's log (COM1) goes to `build/vbox/serial.log`. To run a self-test image instead, or type into the VM during a test:
```
wsl IMAGE=build/test-user/litekernx.img "WAIT_FOR=^selftest: user" bash vm/vbox.sh test
wsl "WAIT_FOR=^kbd: ready" "TYPE=Hi!" "TYPED=kbd: key 0x002 .!." bash vm/vbox.sh test
```

## Logs, screenshots and recordings
- In QEMU, the log (COM1) goes to the terminal and to `build/serial.log`. On the Eee PC, which has no serial port, the **Log** app shows it. **VM timings don't count toward the [boot budget](BOOT-BUDGET.md).**
- [`tests/lib/drive.py`](../tests/lib/drive.py) drives a headless QEMU from a little script (move the pointer, click, type, take screenshots, record MP4s). The README's screenshots were made with it.

## Where things are
```
LiteKernX-Roadmap/   the plan, phase by phase
boot/                the bootloader (stage 1 MBR, stage 2), boot_info, the BIOS thunk
kernel/              the kernel: memory and paging, interrupts, the driver layer, ring 3
                     apps and their system calls (lkx.c, sys_app.c, user.c), the window
                     system (wm.c), the shell and compositor (desktop.c), graphics
                     (gfx.c, text.c, theme.c), FAT32, ACPI power-off
drivers/             hardware drivers behind driver_t: serial, VBE display, PS/2 keyboard
                     and touchpad, RTC, BIOS disk, ATA
apps/                the KERN86 apps (Files, Calculator, Settings); one folder each
sdk/                 what apps are built with (kern86.h and friends)
assets/              logo, icons, cursors, wallpapers, the GUI font
tools/               build-time converters (fonts, icons, wallpapers, the ramdisk) and setup
docs/                this guide and the rest (below)
tests/               bootloader, FAT32 and kernel tests; test apps; the QEMU helpers
vm/                  the QEMU and VirtualBox dev VMs
```

## More docs
- [Making an app](APPS.md): KERN86 apps, their manifest, the SDK
- [Testing on the real Eee PC](HARDWARE-TEST.md)
- [Boot protocol](BOOT-PROTOCOL.md) and [boot-time budget](BOOT-BUDGET.md)
- [Asset rules and prompts](ASSET-PROMPTS.md): icons, cursors, wallpapers, the logo
- [Non-Goals](NON-GOALS.md): what LiteKern X deliberately doesn't do

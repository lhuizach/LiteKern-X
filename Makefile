# LiteKern X — top-level build.
# Run inside WSL/Linux. From PowerShell in the project folder: `wsl make <target>`.

# Overridable so tests can build variants side by side, e.g.
#   make BUILD=build/test-stub KERNEL=stub build/test-stub/litekernx.img
BUILD  ?= build
KERNEL ?= c                 # c = the real kernel, stub = tests/boot/kernel-stub.asm
EXTRA_CFLAGS ?=
EXTRA_KERNEL_SRCS ?=        # extra C / .asm files linked into the kernel (test builds)

QEMU  := bash vm/qemu.sh
IMAGE := $(BUILD)/litekernx.img
NASM  := nasm -f bin -I./

CC      := gcc
LD      := ld
OBJCOPY := objcopy
CFLAGS  := -m32 -march=i686 -mtune=bonnell -std=gnu11 -O2 -g \
           -ffreestanding -fno-pie -fno-pic -fno-stack-protector \
           -fno-asynchronous-unwind-tables -fcf-protection=none \
           -mgeneral-regs-only -Wall -Wextra -Werror -I. $(EXTRA_CFLAGS)
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

KERNEL_SRCS := $(wildcard kernel/*.c drivers/*.c kernel/*.asm) $(EXTRA_KERNEL_SRCS)
KERNEL_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(filter %.c,$(KERNEL_SRCS))) \
               $(patsubst %.asm,$(BUILD)/%.asm.o,$(filter %.asm,$(KERNEL_SRCS))) \
               $(BUILD)/gen/cursors.o

# Cursors: assets/cursors.json + PNGs -> C, at build time (docs/ASSET-PROMPTS.md §3).
CURSOR_ASSETS := assets/cursors.json $(wildcard assets/cursors/*.png)

# Ring 3 test programs: flat binaries at USER_BASE, embedded into self-test
# kernels by tests/kernel/user_programs.asm.
USER_TEST_BINS := $(patsubst %.asm,$(BUILD)/%.user.bin,$(wildcard tests/kernel/user/*.asm))

.PHONY: all run debug test test-boot test-kernel smoke smoke-gui check-tools clean \
        vbox-create vbox vbox-test usb

all: $(IMAGE)

# --- kernel -----------------------------------------------------------------

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/%.asm.o: %.asm boot/bootinfo.inc
	@mkdir -p $(dir $@)
	nasm -f elf32 -I./ -DUSER_BIN_DIR='"$(BUILD)/tests/kernel/user"' -o $@ $<

$(BUILD)/tests/kernel/user_programs.asm.o: $(USER_TEST_BINS)

$(BUILD)/gen/cursors.c: $(CURSOR_ASSETS) tools/cursors2c.py tools/lkx_png.py
	@mkdir -p $(dir $@)
	python3 tools/cursors2c.py assets/cursors.json $@

$(BUILD)/gen/cursors.o: $(BUILD)/gen/cursors.c kernel/cursor.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.user.bin: %.asm tests/kernel/user/user.inc
	@mkdir -p $(dir $@)
	$(NASM) -o $@ $<

$(BUILD)/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) -m elf_i386 --no-warn-rwx-segments -T kernel/linker.ld -o $@ $(KERNEL_OBJS) $(LIBGCC)

ifeq ($(strip $(KERNEL)),stub)
$(BUILD)/kernel.bin: tests/boot/kernel-stub.asm boot/bootinfo.inc | $(BUILD)
	$(NASM) -o $@ $<
else
# Flat binary for stage 2. The header's file_size (offset 16) must match the
# file exactly, or stage 2 would read past the end of the image.
$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@size=$$(stat -c %s $@); hdr=$$(od -An -t u4 -j 16 -N 4 $@ | tr -d ' '); \
	if [ "$$size" -ne "$$hdr" ]; then \
		echo "error: kernel.bin is $$size bytes but its header says $$hdr"; rm -f $@; exit 1; \
	fi
endif

-include $(KERNEL_OBJS:.o=.d)

# --- boot image -------------------------------------------------------------
# Layout (docs/BOOT-PROTOCOL.md): stage 1 | stage 2 (whole sectors) | kernel

$(BUILD)/stage2.bin: boot/stage2.asm boot/bootinfo.inc | $(BUILD)
	$(NASM) -o $@ $<

# Stage 1 needs stage 2's size and the total image size baked in.
$(BUILD)/stage1.bin: boot/stage1.asm $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	@s2=$$(( $$(stat -c %s $(BUILD)/stage2.bin) / 512 )); \
	k=$$(( ($$(stat -c %s $(BUILD)/kernel.bin) + 511) / 512 )); \
	echo "nasm stage1 (stage 2 = $$s2 sectors, kernel = $$k sectors)"; \
	$(NASM) -DSTAGE2_SECTORS=$$s2 -DDISK_SECTORS=$$((1 + s2 + k)) -o $@ $<

$(IMAGE): $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	cat $^ > $@
	truncate -s %512 $@

run: $(IMAGE)
	$(QEMU) --image $(IMAGE)

debug: $(IMAGE)
	$(QEMU) --image $(IMAGE) --debug

# --- tests ------------------------------------------------------------------

test: smoke test-boot test-kernel

test-boot:
	@bash tests/boot/test-stage1.sh
	@echo
	@bash tests/boot/test-stage2.sh

test-kernel:
	@bash tests/kernel/test-kernel.sh

# --- real hardware (docs/HARDWARE-TEST.md) ----------------------------------
# The normal image and the ring 3 self-test image, padded to 1 MiB so USB
# writing tools (Rufus, balenaEtcher) accept them.

USB_TEST_FLAGS := BUILD=build/test-user EXTRA_CFLAGS=-DLKX_SELFTEST_USER \
                  "EXTRA_KERNEL_SRCS=tests/kernel/selftest_user.c tests/kernel/user_programs.asm"

usb: $(IMAGE)
	@$(MAKE) -s $(USB_TEST_FLAGS) build/test-user/litekernx.img
	@$(MAKE) -s BUILD=build/diag-vbios EXTRA_CFLAGS=-DLKX_DIAG_VBIOS build/diag-vbios/litekernx.img
	@$(MAKE) -s BUILD=build/test-gfx EXTRA_CFLAGS=-DLKX_SELFTEST_GFX \
		EXTRA_KERNEL_SRCS=tests/kernel/selftest_gfx.c build/test-gfx/litekernx.img
	@mkdir -p build/usb
	@cp $(IMAGE) build/usb/litekernx.img
	@cp build/test-user/litekernx.img build/usb/litekernx-ring3-test.img
	@cp build/diag-vbios/litekernx.img build/usb/litekernx-vbios-diag.img
	@cp build/test-gfx/litekernx.img build/usb/litekernx-gfx-test.img
	@truncate -s 1M build/usb/litekernx.img build/usb/litekernx-ring3-test.img \
		build/usb/litekernx-vbios-diag.img build/usb/litekernx-gfx-test.img
	@echo "USB images (write one at a time; see docs/HARDWARE-TEST.md):"
	@echo "  build/usb/litekernx.img             normal boot"
	@echo "  build/usb/litekernx-ring3-test.img  ring 3 self-test"
	@echo "  build/usb/litekernx-vbios-diag.img  video BIOS diagnostic (1024x600 fix)"
	@echo "  build/usb/litekernx-gfx-test.img    rendering pipeline self-test + timings"

# --- VirtualBox VM (vm/vbox.sh) ---------------------------------------------
# Not part of `make test`: it needs the Windows VirtualBox install.

vbox-create:
	@bash vm/vbox.sh create

vbox: $(IMAGE)
	@bash vm/vbox.sh start

vbox-test: $(IMAGE)
	@bash vm/vbox.sh test

# --- VM smoke test ----------------------------------------------------------

$(BUILD)/smoke.img: vm/smoke/smoke.asm | $(BUILD)
	nasm -f bin -o $@ $<

# Headless: boots the smoke image and checks the serial output + exit code.
smoke: $(BUILD)/smoke.img
	@set +e; \
	out=$$(timeout 30 $(QEMU) --headless --image $<); rc=$$?; \
	printf '%s\n' "$$out"; \
	if [ $$rc -eq 33 ] && printf '%s' "$$out" | grep -q 'smoke test: OK'; then \
		echo "PASS: VM boots the image and serial logging works"; \
	else \
		echo "FAIL: qemu exit status $$rc (expected 33)"; exit 1; \
	fi

# Same image in a window, so you can see the VM.
smoke-gui: $(BUILD)/smoke.img
	$(QEMU) --image $<

# --- misc -------------------------------------------------------------------

check-tools:
	@missing=0; \
	for t in gcc ld nasm gdb qemu-system-i386; do \
		if command -v $$t >/dev/null; then echo "ok       $$t"; \
		else echo "MISSING  $$t"; missing=1; fi; \
	done; \
	if echo 'int x;' | gcc -m32 -ffreestanding -fno-pie -c -x c - -o /dev/null 2>/dev/null; \
	then echo "ok       gcc -m32 (freestanding i386)"; \
	else echo "MISSING  gcc -m32 support (gcc-multilib)"; missing=1; fi; \
	if [ $$missing -ne 0 ]; then echo "Install with: sudo bash tools/setup-wsl.sh"; exit 1; fi

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

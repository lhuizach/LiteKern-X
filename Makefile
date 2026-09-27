# LiteKern X — top-level build.
# Run inside WSL/Linux. From PowerShell in the project folder: `wsl make <target>`.

BUILD := build
QEMU  := bash vm/qemu.sh
IMAGE := $(BUILD)/litekernx.img

# Until the real stage 2 exists (Phase 1 section 2), images boot a test stub.
STAGE2_SRC := tests/boot/stage2-stub.asm

.PHONY: all run debug test test-boot smoke smoke-gui check-tools clean

all: $(IMAGE)

# --- boot image -------------------------------------------------------------

$(BUILD)/stage2.bin: $(STAGE2_SRC) | $(BUILD)
	nasm -f bin -o $@ $<

# Stage 1 needs stage 2's size baked in.
$(BUILD)/stage1.bin: boot/stage1.asm $(BUILD)/stage2.bin
	@s2=$$(( ($$(stat -c %s $(BUILD)/stage2.bin) + 511) / 512 )); \
	echo "nasm stage1 (stage 2 = $$s2 sectors)"; \
	nasm -f bin -DSTAGE2_SECTORS=$$s2 -DDISK_SECTORS=$$((1 + s2)) -o $@ $<

$(IMAGE): $(BUILD)/stage1.bin $(BUILD)/stage2.bin
	cat $^ > $@
	truncate -s %512 $@

run: $(IMAGE)
	$(QEMU) --image $(IMAGE)

debug: $(IMAGE)
	$(QEMU) --image $(IMAGE) --debug

# --- tests ------------------------------------------------------------------

test: smoke test-boot

test-boot:
	@bash tests/boot/test-stage1.sh

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

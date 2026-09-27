#!/usr/bin/env bash
# LiteKern X — VirtualBox VM, a second dev VM alongside QEMU (vm/qemu.sh).
# Run from WSL; drives the Windows VirtualBox install through VBoxManage.exe.
#
# Hardware profile, approximating the ASUS EeePC 1000HE:
#   1 CPU, 1 GiB RAM, PIIX3 chipset, legacy BIOS, VBoxVGA (VBE), IDE disk,
#   PS/2 keyboard + mouse; no network, audio or USB (all Non-Goals).
#   A custom 1024x600x32 VBE mode, like the EeePC panel.
#   COM1 -> build/vbox/serial.log (the boot log).
#
# VirtualBox can't boot a raw image directly, so every `update` converts
# build/litekernx.img to a VDI with a fixed UUID and re-attaches it.
#
# As with QEMU: timings measured here don't count against the boot budget.
set -euo pipefail
cd "$(dirname "$0")/.."

VBM=${VBOXMANAGE:-"/mnt/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"}
VM="LiteKern X"
DISK_UUID=4c4b5800-0000-4000-8000-000000000001
IMAGE=${IMAGE:-build/litekernx.img}                 # e.g. IMAGE=build/test-user/litekernx.img
WAIT_FOR=${WAIT_FOR:-'^\[boot\] ready'}             # `test` passes once a log line matches
TYPE=${TYPE:-}                                      # `test`: text to type once WAIT_FOR matched,
TYPED=${TYPED:-}                                    #   then wait for this regex too
DIR=build/vbox
VDI=$DIR/litekernx.vdi
SERIAL_LOG=$DIR/serial.log

usage() {
    cat <<'EOF'
usage: vm/vbox.sh COMMAND

  create     create and register the "LiteKern X" VM (once)
  update     convert build/litekernx.img to the VM's disk
  start      update, then boot the VM in a window
  test       update, boot headless, wait for "[boot] ready" (or a panic),
             save a screenshot to build/vbox/screen.png, power off.
             IMAGE=... boots another image; WAIT_FOR=regex changes the line
             waited for (e.g. a self-test's summary); TYPE="text" types it
             into the VM afterwards and TYPED=regex waits for the result
  stop       power the VM off
  status     show the VM's state
  destroy    unregister the VM and delete its files
EOF
}

vbm() { "$VBM" "$@" | tr -d '\r'; }
win() { wslpath -w "$(realpath -m "$1")"; }     # VirtualBox needs absolute Windows paths
exists() { "$VBM" showvminfo "$VM" >/dev/null 2>&1; }
state() { vbm showvminfo "$VM" --machinereadable | sed -n 's/^VMState="\(.*\)"/\1/p'; }
need_vm() { exists || { echo "VM \"$VM\" doesn't exist. Run: vm/vbox.sh create" >&2; exit 1; }; }
need_off() {
    case "$(state)" in
        poweroff|aborted|saved) ;;
        *) echo "VM \"$VM\" is running. Run: vm/vbox.sh stop" >&2; exit 1 ;;
    esac
}

create() {
    [ -x "$VBM" ] || { echo "VBoxManage not found at $VBM (set VBOXMANAGE)" >&2; exit 1; }
    if exists; then echo "VM \"$VM\" already exists"; return; fi
    mkdir -p "$DIR"
    vbm createvm --name "$VM" --ostype Other --register
    vbm modifyvm "$VM" \
        --memory 1024 --cpus 1 --chipset piix3 --firmware bios \
        --ioapic off --pae off --hwvirtex on \
        --graphicscontroller vboxvga --vram 16 \
        --keyboard ps2 --mouse ps2 \
        --audio-enabled off --usb-ohci off --usb-ehci off --usb-xhci off \
        --nic1 none \
        --boot1 disk --boot2 none --boot3 none --boot4 none \
        --uart1 0x3F8 4 --uart-mode1 file "$(win "$SERIAL_LOG")"
    vbm storagectl "$VM" --name IDE --add ide --controller PIIX4
    # Advertise the EeePC panel's mode in the VBE mode list, so stage 2's
    # preferred 1024x600 path runs here (QEMU can't do this).
    vbm setextradata "$VM" CustomVideoMode1 1024x600x32
    echo "created VM \"$VM\""
}

update() {
    need_vm
    need_off
    [ -f "$IMAGE" ] || { echo "$IMAGE missing. Run: make" >&2; exit 1; }
    mkdir -p "$DIR"

    # Detach and forget the old disk (ignore errors: it may not exist yet).
    "$VBM" storageattach "$VM" --storagectl IDE --port 0 --device 0 --medium none >/dev/null 2>&1 || true
    "$VBM" closemedium disk "$DISK_UUID" --delete >/dev/null 2>&1 || true
    rm -f "$VDI"

    # Pad to 1 MiB: VirtualBox is happier with a disk that isn't a few KiB.
    cp "$IMAGE" "$DIR/disk.raw"
    [ "$(stat -c %s "$DIR/disk.raw")" -ge 1048576 ] || truncate -s 1M "$DIR/disk.raw"
    vbm convertfromraw "$(win "$DIR/disk.raw")" "$(win "$VDI")" --format VDI --uuid "$DISK_UUID" >/dev/null
    rm -f "$DIR/disk.raw"
    vbm storageattach "$VM" --storagectl IDE --port 0 --device 0 --type hdd --medium "$(win "$VDI")"
    echo "disk updated from $IMAGE"
}

start() {
    update
    rm -f "$SERIAL_LOG"
    vbm startvm "$VM" --type gui
    echo "serial log: $SERIAL_LOG"
}

test_boot() {
    update
    rm -f "$SERIAL_LOG" "$DIR/screen.png"
    vbm startvm "$VM" --type headless >/dev/null
    local i result=timeout
    for ((i = 0; i < 600; i++)); do
        if tr -d '\r' <"$SERIAL_LOG" 2>/dev/null | grep -Eq "$WAIT_FOR"; then result=ready; break; fi
        if grep -q 'PANIC' "$SERIAL_LOG" 2>/dev/null; then result=panic; break; fi
        sleep 0.1
    done
    if [ "$result" = ready ] && [ -n "$TYPE" ]; then
        sleep 0.5
        vbm controlvm "$VM" keyboardputstring "$TYPE"
        result=timeout
        for ((i = 0; i < 100; i++)); do
            if tr -d '\r' <"$SERIAL_LOG" | grep -Eq "$TYPED"; then result=ready; break; fi
            sleep 0.1
        done
    fi
    sleep 1     # let the status colour reach the screen
    vbm controlvm "$VM" screenshotpng "$(win "$DIR/screen.png")" || true
    vbm controlvm "$VM" poweroff >/dev/null 2>&1 || true

    tr -d '\r' <"$SERIAL_LOG" 2>/dev/null | sed '/^$/d; s/^/      /'
    case $result in
        ready)   echo "PASS  VirtualBox boots $IMAGE (screenshot: $DIR/screen.png)" ;;
        panic)   echo "FAIL  kernel panicked in VirtualBox"; exit 1 ;;
        timeout) echo "FAIL  no line matching $WAIT_FOR within 60 s"; exit 1 ;;
    esac
}

case "${1:-}" in
    create)  create ;;
    update)  update ;;
    start)   start ;;
    test)    test_boot ;;
    stop)    need_vm; vbm controlvm "$VM" poweroff ;;
    status)  need_vm; echo "$VM: $(state)" ;;
    destroy) need_vm; need_off; vbm unregistervm "$VM" --delete ;;
    -h|--help|"") usage ;;
    *) usage >&2; exit 2 ;;
esac

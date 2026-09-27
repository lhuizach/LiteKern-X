; LiteKern X — VM smoke test. NOT the real bootloader (that's Phase 1 §2).
;
; A single boot sector that proves the VM works end to end:
;   - the BIOS boots our raw disk image
;   - COM1 serial output reaches the host (this is how boot timings get logged)
;   - isa-debug-exit lets a guest end a headless run with a status code
;
; Build: nasm -f bin -o build/smoke.img vm/smoke/smoke.asm

bits 16
org 0x7c00

COM1         equ 0x3f8
DEBUG_EXIT   equ 0xf4
EXIT_PASS    equ 0x10               ; QEMU exit status = (0x10 << 1) | 1 = 33

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    cld

    ; COM1: 115200 baud, 8N1, FIFOs on
    mov dx, COM1 + 1                ; IER: no interrupts
    xor al, al
    out dx, al
    mov dx, COM1 + 3                ; LCR: DLAB on
    mov al, 0x80
    out dx, al
    mov dx, COM1 + 0                ; divisor low = 1
    mov al, 1
    out dx, al
    mov dx, COM1 + 1                ; divisor high = 0
    xor al, al
    out dx, al
    mov dx, COM1 + 3                ; LCR: 8N1, DLAB off
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 2                ; FCR: enable + clear FIFOs
    mov al, 0xc7
    out dx, al

    mov si, msg
.next:
    lodsb
    test al, al
    jz .done
    mov bl, al

    mov dx, COM1 + 5                ; LSR: wait for empty transmit register
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, COM1
    mov al, bl
    out dx, al

    mov ah, 0x0e                    ; BIOS teletype, so the window shows it too
    mov al, bl
    xor bh, bh
    int 0x10
    jmp .next

.done:
    ; Headless runs: exit QEMU with the pass code. GUI runs have no
    ; isa-debug-exit device, so this write is ignored and we just halt.
    mov dx, DEBUG_EXIT
    mov al, EXIT_PASS
    out dx, al
.halt:
    hlt
    jmp .halt

msg db "LiteKern X VM smoke test: OK", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xaa55

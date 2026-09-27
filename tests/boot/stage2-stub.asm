; LiteKern X — stand-in for stage 2, used until the real one exists.
;
; Checks the stage 1 handoff (docs/BOOT-PROTOCOL.md) and reports over COM1:
;   - it was entered at all (magic accepted, jump target right)
;   - every sector was loaded (end marker in the last sector is present)
;   - DL and T0 were handed over
; Then exits QEMU via isa-debug-exit: 0x10 -> status 33 (pass), 0x11 -> 35 (fail).
;
; Padded to 8 sectors so stage 1's multi-sector read is actually exercised.

bits 16
org 0x8000

COM1       equ 0x3f8
DEBUG_EXIT equ 0xf4
BOOT_T0    equ 0x0500
SECTORS    equ 8

    dd 'LKX2'

entry:
    mov [drive], dl
    rdtsc
    sub eax, [BOOT_T0]
    sbb edx, [BOOT_T0 + 4]
    mov [ticks], eax
    mov [ticks + 4], edx

    cmp dword [end_marker], 'END2'
    jne .fail

    mov si, msg_ok
    call print
    movzx eax, byte [drive]
    mov cx, 2
    call print_hex
    mov si, msg_ticks
    call print
    mov eax, [ticks + 4]
    mov cx, 8
    call print_hex
    mov eax, [ticks]
    mov cx, 8
    call print_hex
    mov si, crlf
    call print
    mov al, 0x10
    jmp .exit

.fail:
    mov si, msg_fail
    call print
    mov al, 0x11
.exit:
    out DEBUG_EXIT, al
.halt:
    cli
    hlt
    jmp .halt

; Print NUL-terminated DS:SI to COM1.
print:
    lodsb
    test al, al
    jz .done
    call putc
    jmp print
.done:
    ret

; Print the low CX hex digits of EAX.
print_hex:
    push cx
    mov bx, cx
    shl bx, 2
    mov cl, 32
    sub cl, bl
    shl eax, cl                         ; top digit wanted -> top of EAX
    pop cx
.digit:
    rol eax, 4
    push eax
    and al, 0x0f
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 'a' - '9' - 1
.out:
    call putc
    pop eax
    loop .digit
    ret

putc:
    push ax
    mov dx, COM1 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, COM1
    out dx, al
    ret

msg_ok    db "stage2-stub: OK drive=", 0
msg_ticks db " stage1_ticks=0x", 0
msg_fail  db "stage2-stub: FAIL end marker missing (partial load)", 13, 10, 0
crlf      db 13, 10, 0
drive     db 0
ticks     dq 0

    times SECTORS * 512 - 4 - ($ - $$) db 0
end_marker:
    dd 'END2'

; LiteKern X — stand-in kernel, used until the real kernel exists
; (Phase 1 §2, "Kernel entry + minimal init").
;
; Verifies everything stage 2 promises (docs/BOOT-PROTOCOL.md), fills the
; framebuffer so a GUI run shows it worked, and reports over COM1. Then exits
; QEMU via isa-debug-exit: 0x10 -> status 33 (pass), 0x11 -> 35 (fail).
;
; Padded to 200 KiB so stage 2's multi-chunk read path is exercised.

bits 32
org 0x100000

%include "boot/bootinfo.inc"

COM1        equ 0x3f8
DEBUG_EXIT  equ 0xf4
FILE_SIZE   equ 200 * 1024
BSS_SIZE    equ 64 * 1024
BG_COLOUR   equ 0x001e3a5f
BAND_COLOUR equ 0x0048a6e8

header:
    dd KERNEL_MAGIC
    dd KERNEL_VERSION
    dd header                       ; load address
    dd entry
    dd file_end - header            ; file size
    dd bss_end                      ; mem end

entry:
    ; Nothing may touch the stack or bss before the bss check.
    mov ebp, ebx
    mov esi, msg_handoff
    cmp eax, BOOT_INFO_MAGIC
    jne fail

    mov esi, msg_bss
    mov edi, bss_start
    mov ecx, (bss_end - bss_start) / 4
    xor eax, eax
    repe scasd
    jne fail

    mov esp, stack_top
    mov [bi_ptr], ebp

    mov esi, msg_bi
    cmp dword [ebp + bi.magic], BOOT_INFO_MAGIC
    jne fail
    cmp dword [ebp + bi.version], BOOT_INFO_VERSION
    jne fail

    mov esi, msg_pmode
    mov eax, cr0
    test al, 1
    jz fail

    mov esi, msg_copy
    cmp dword [end_marker], 'ENDK'
    jne fail
    cmp dword [ebp + bi.kernel_start], header
    jne fail
    cmp dword [ebp + bi.kernel_end], bss_end
    jne fail

    ; A20: a write above 1 MiB must not show up at its 1 MiB-lower alias.
    mov esi, msg_a20
    mov dword [a20_probe - 0x100000], 0xaaaaaaaa
    mov dword [a20_probe], 0x55555555
    cmp dword [a20_probe - 0x100000], 0xaaaaaaaa
    jne fail

    ; TSC marks: all set, never going backwards.
    mov esi, msg_tsc
    xor ecx, ecx
.tsc:
    mov eax, [ebp + bi.tsc + ecx * 8]
    mov edx, [ebp + bi.tsc + ecx * 8 + 4]
    mov ebx, eax
    or ebx, edx
    jz fail
    test ecx, ecx
    jz .tsc_next
    cmp edx, [ebp + bi.tsc + ecx * 8 - 4]
    jb fail
    ja .tsc_next
    cmp eax, [ebp + bi.tsc + ecx * 8 - 8]
    jb fail
.tsc_next:
    inc ecx
    cmp ecx, TSC_COUNT
    jb .tsc

    ; Memory map: some entries, usable RAM above 1 MiB. Sum usable MiB.
    mov esi, msg_mmap
    mov ecx, [ebp + bi.mmap_count]
    test ecx, ecx
    jz fail
    cmp ecx, BOOT_MMAP_MAX
    ja fail
    mov edi, [ebp + bi.mmap_addr]
    xor ebx, ebx                    ; usable MiB
    xor edx, edx                    ; saw usable RAM at or above 1 MiB?
.mmap:
    cmp dword [edi + 16], 1         ; type 1 = usable
    jne .mmap_next
    mov eax, [edi + 12]             ; length high
    shl eax, 12
    add ebx, eax
    mov eax, [edi + 8]              ; length low
    shr eax, 20
    add ebx, eax
    cmp dword [edi + 4], 0
    jne .high
    cmp dword [edi], 0x100000
    jb .mmap_next
.high:
    mov edx, 1
.mmap_next:
    add edi, E820_ENTRY_SIZE
    loop .mmap
    test edx, edx
    jz fail
    mov [usable_mib], ebx

    ; Framebuffer.
    mov esi, msg_fb
    test dword [ebp + bi.flags], BI_FLAG_FB
    jz fail
    cmp dword [ebp + bi.fb_bpp], 32
    jne fail
    mov eax, [ebp + bi.fb_width]
    test eax, eax
    jz fail
    shl eax, 2
    cmp [ebp + bi.fb_pitch], eax
    jb fail
    cmp dword [ebp + bi.fb_height], 0
    je fail
    call paint

    ; --- report ---
    mov esi, msg_ok
    call print
    mov eax, [usable_mib]
    call print_dec
    mov esi, msg_mmap_count
    call print
    mov eax, [ebp + bi.mmap_count]
    call print_dec
    mov esi, msg_fb_size
    call print
    mov eax, [ebp + bi.fb_width]
    call print_dec
    mov al, 'x'
    call putc
    mov eax, [ebp + bi.fb_height]
    call print_dec
    mov al, 'x'
    call putc
    mov eax, [ebp + bi.fb_bpp]
    call print_dec
    mov esi, msg_fb_addr
    call print
    mov eax, [ebp + bi.fb_addr]
    call print_hex
    mov esi, crlf
    call print

    mov esi, msg_ticks
    call print
    mov ebx, 0
    mov esi, name_stage2
    call print_tsc_delta
    mov ebx, 1
    mov esi, name_loaded
    call print_tsc_delta
    mov ebx, 2
    mov esi, name_vbe
    call print_tsc_delta
    mov ebx, 3
    mov esi, name_entry
    call print_tsc_delta
    mov esi, crlf
    call print

    mov al, 0x10
    out DEBUG_EXIT, al
    jmp halt

fail:
    push esi
    mov esi, msg_fail
    call print
    pop esi
    call print
    mov esi, crlf
    call print
    mov al, 0x11
    out DEBUG_EXIT, al
halt:
    cli
    hlt
    jmp halt

; Fill the screen: background with a band across the middle third.
paint:
    mov edi, [ebp + bi.fb_addr]
    mov ecx, [ebp + bi.fb_height]
    xor edx, edx                    ; y
.row:
    mov eax, BG_COLOUR
    mov ebx, [ebp + bi.fb_height]
    push edx
    imul edx, edx, 3
    cmp edx, ebx
    jb .colour
    shl ebx, 1
    cmp edx, ebx
    jae .colour
    mov eax, BAND_COLOUR
.colour:
    pop edx
    push edi
    push ecx
    mov ecx, [ebp + bi.fb_width]
    rep stosd
    pop ecx
    pop edi
    add edi, [ebp + bi.fb_pitch]
    inc edx
    loop .row
    ret

; Print " name=<ticks>" for tsc[EBX+1] - tsc[EBX] (low 32 bits of the delta).
print_tsc_delta:
    call print
    mov eax, [ebp + bi.tsc + ebx * 8 + 8]
    sub eax, [ebp + bi.tsc + ebx * 8]
    call print_dec
    ret

print:
    push eax
.next:
    lodsb
    test al, al
    jz .done
    call putc
    jmp .next
.done:
    pop eax
    ret

print_dec:
    push ebx
    push ecx
    push edx
    mov ebx, 10
    xor ecx, ecx
.div:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .div
.out:
    pop eax
    add al, '0'
    call putc
    loop .out
    pop edx
    pop ecx
    pop ebx
    ret

print_hex:
    push ecx
    mov ecx, 8
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
    pop ecx
    ret

putc:
    push edx
    push eax
    mov dx, COM1 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    pop eax
    mov dx, COM1
    out dx, al
    pop edx
    ret

msg_ok          db "kernel-stub: OK usable=", 0
msg_mmap_count  db "MiB mmap=", 0
msg_fb_size     db " fb=", 0
msg_fb_addr     db " fb@0x", 0
msg_ticks       db "kernel-stub: tsc", 0
name_stage2     db " stage1->stage2=", 0
name_loaded     db " stage2->loaded=", 0
name_vbe        db " loaded->vbe=", 0
name_entry      db " vbe->kernel=", 0
crlf            db 13, 10, 0

msg_fail        db "kernel-stub: FAIL ", 0
msg_handoff     db "EAX is not the boot_info magic", 0
msg_bss         db "bss not zeroed", 0
msg_bi          db "bad boot_info magic/version", 0
msg_pmode       db "not in protected mode", 0
msg_copy        db "kernel image incomplete or misplaced", 0
msg_a20         db "A20 disabled", 0
msg_tsc         db "TSC marks missing or out of order", 0
msg_mmap        db "memory map empty or no RAM above 1 MiB", 0
msg_fb          db "framebuffer info invalid", 0

bi_ptr          dd 0
usable_mib      dd 0

    times FILE_SIZE - 4 - ($ - $$) db 0
end_marker:
    dd 'ENDK'
file_end:

absolute file_end
bss_start:
a20_probe:
    resd 1
    resb BSS_SIZE - 4
stack_top:
bss_end:

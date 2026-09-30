; LiteKern X — the real-mode BIOS disk thunk (Phase 2 §5a), ported from v1's
; boot/realmode_thunk.asm.
;
; Lets the running kernel (32-bit, paging on) make one INT 13h call: it drops
; to real mode, calls the BIOS with the registers from the parameter block,
; stores what the BIOS returned, and comes back with everything restored. The
; BIOS is the only thing that knows how to reach the USB stick it booted from
; (the EeePC has no USB driver of ours), so this is how Files reads and writes
; the stick.
;
; Assembled as a flat binary (ORG 0) and copied to physical BASE by
; drivers/bios_disk.c. BASE is in stage 2's kernel bounce buffer (0x10000-
; 0x7ffff), which is dead after boot and never given out by the frame
; allocator (it only manages RAM above 1 MiB). The kernel identity-maps it,
; which is what lets this code keep running as paging is switched off and on.
;
; Layout (offsets from BASE; drivers/bios_disk.c must match):
;   0x000  code entry (a near call from the kernel)
;   0x400  parameter block: in  ax bx cx dx si (5 words)
;                           out ax bx cx dx flags (5 words)
;   0x420  disk address packet / drive parameter buffer (0x40 bytes)
;   0xff0  top of the real-mode stack
;   0x1000 data buffer (bios_disk.c: up to 64 sectors)
;
; Interrupts stay off throughout and every PIC line is masked, as in v1: an
; IRQ mid-switch would go through whichever table is live at that instant.

bits 32
org 0

BASE        equ 0x20000
SEG16       equ BASE >> 4
STACK_TOP   equ 0xff0

entry:
    jmp code

align 8
gdt:
    dq 0                                        ; null
    dq 0x00cf9a000000ffff                       ; 0x08: code32 flat (as the kernel's)
    dq 0x00cf92000000ffff                       ; 0x10: data32 flat (as the kernel's)
    dw 0xffff, BASE & 0xffff                    ; 0x18: code16, base BASE, 64 KiB
    db BASE >> 16, 0x9a, 0x00, 0x00
    dw 0xffff, BASE & 0xffff                    ; 0x20: data16, base BASE, 64 KiB
    db BASE >> 16, 0x92, 0x00, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt - 1
    dd BASE + gdt

rm_idt:                                         ; the real-mode vector table
    dw 0x3ff
    dd 0

align 4
saved_gdt:  dw 0, 0, 0
saved_idt:  dw 0, 0, 0
saved_esp:  dd 0
saved_cr0:  dd 0
pic1_mask:  db 0
pic2_mask:  db 0

times 0x400 - ($ - $$) db 0
params:
in_ax:      dw 0
in_bx:      dw 0
in_cx:      dw 0
in_dx:      dw 0
in_si:      dw 0
out_ax:     dw 0
out_bx:     dw 0
out_cx:     dw 0
out_dx:     dw 0
out_flags:  dw 0
times 0x420 - ($ - $$) db 0
packet:     times 0x40 db 0

align 16
code:
    pushad
    pushfd
    cli
    in al, 0x21                                 ; mask every IRQ (restored below)
    mov [BASE + pic1_mask], al
    in al, 0xa1
    mov [BASE + pic2_mask], al
    mov al, 0xff
    out 0x21, al
    out 0xa1, al

    sgdt [BASE + saved_gdt]
    sidt [BASE + saved_idt]
    mov [BASE + saved_esp], esp
    mov eax, cr0
    mov [BASE + saved_cr0], eax

    lgdt [BASE + gdt_desc]
    mov esp, BASE + STACK_TOP
    mov eax, cr0                                ; paging off: this code is identity-mapped
    and eax, 0x7fffffff
    mov cr0, eax
    jmp dword 0x18:pm16

bits 16
pm16:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    mov eax, cr0                                ; protection off: real mode
    and al, 0xfe
    mov cr0, eax
    jmp SEG16:real

real:
    mov ax, SEG16
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, STACK_TOP                          ; all 32 bits: some BIOSes use ESP
    lidt [rm_idt]

    mov ax, [in_ax]
    mov bx, [in_bx]
    mov cx, [in_cx]
    mov dx, [in_dx]
    mov si, [in_si]
    int 0x13
    mov [out_ax], ax
    mov [out_bx], bx
    mov [out_cx], cx
    mov [out_dx], dx
    pushf
    pop word [out_flags]
    cli                                         ; in case the BIOS enabled interrupts

    mov eax, cr0                                ; protection back on (16-bit)
    or al, 1
    mov cr0, eax
    jmp 0x18:pm16_back

pm16_back:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr0                                ; paging back on (CR3 unchanged)
    or eax, 0x80000000
    mov cr0, eax
    o32 lgdt [saved_gdt]                        ; DS base is BASE: this is BASE + saved_gdt
    jmp dword 0x08:(BASE + pm32_back)

bits 32
pm32_back:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    lidt [BASE + saved_idt]
    mov al, [BASE + pic1_mask]
    out 0x21, al
    mov al, [BASE + pic2_mask]
    out 0xa1, al
    mov eax, [BASE + saved_cr0]                 ; e.g. CR0.WP, exactly as it was
    mov cr0, eax
    mov esp, [BASE + saved_esp]
    popfd
    popad
    ret

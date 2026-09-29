; LiteKern X — stage 2 bootloader.
;
; Entered from stage 1 in real mode (see docs/BOOT-PROTOCOL.md). In order:
;   1. init COM1, set up boot_info, copy T0
;   2. E820 memory map
;   3. enable A20
;   4. load the kernel to the 0x10000 bounce buffer and validate its header
;   5. set the VBE mode (last BIOS text output possible before this)
;   6. switch to 32-bit protected mode, copy the kernel to its load address,
;      zero its bss, jump to it with EAX = 'LKXB', EBX = &boot_info
;
; Every failure prints "LKX stage2: <reason>" to the screen + COM1 and halts.

bits 16
org 0x8000

%include "boot/bootinfo.inc"

COM1            equ 0x3f8
READ_TRIES      equ 3
BOUNCE_SEG      equ 0x1000          ; kernel is read to 0x10000 ...
BOUNCE_LIMIT    equ 0x80000         ; ... and must fit below here (448 KiB)
KERNEL_MAX_SECTORS equ (BOUNCE_LIMIT - BOUNCE_SEG * 16) / 512
KERNEL_MEM_LIMIT equ 0x1000000     ; kernel (incl. bss) must end below 16 MiB
READ_CHUNK      equ 64             ; sectors per read: 32 KiB never crosses a 64 KiB boundary
VBE_INFO        equ 0x3000          ; 512-byte VBE controller info scratch
VBE_MODE_INFO   equ 0x3200          ; 256-byte VBE mode info scratch

; Layout checks against boot/bootinfo.h (assembly fails if they drift).
    times -(bi_size != 160) db 0
    times -(vme_size != VBE_MODE_ENTRY_SIZE) db 0
    times -(kh_size != 24) db 0

    dd 'LKX2'                       ; stage 1 checks this, then jumps to entry

entry:
    mov [boot_drive], dl
    rdtsc
    mov [tsc_stage2], eax
    mov [tsc_stage2 + 4], edx
    call serial_init

    ; boot_info: zero it (and the mmap and VBE mode areas after it), then fill in basics.
    mov di, BOOT_INFO_ADDR
    mov cx, (BOOT_VBE_MODES_ADDR + BOOT_VBE_MODES_MAX * VBE_MODE_ENTRY_SIZE - BOOT_INFO_ADDR) / 2
    xor ax, ax
    rep stosw
    mov dword [BOOT_INFO_ADDR + bi.magic], BOOT_INFO_MAGIC
    mov dword [BOOT_INFO_ADDR + bi.version], BOOT_INFO_VERSION
    movzx eax, byte [boot_drive]
    mov [BOOT_INFO_ADDR + bi.boot_drive], eax
    mov eax, [BOOT_T0_ADDR]
    mov edx, [BOOT_T0_ADDR + 4]
    mov [BOOT_INFO_ADDR + bi.tsc + TSC_STAGE1 * 8], eax
    mov [BOOT_INFO_ADDR + bi.tsc + TSC_STAGE1 * 8 + 4], edx
    mov eax, [tsc_stage2]
    mov edx, [tsc_stage2 + 4]
    mov [BOOT_INFO_ADDR + bi.tsc + TSC_STAGE2 * 8], eax
    mov [BOOT_INFO_ADDR + bi.tsc + TSC_STAGE2 * 8 + 4], edx

    call read_e820
    call enable_a20
    call get_bios_font
    call load_kernel
    mov bx, TSC_KERNEL_LOADED
    call mark_tsc
    call patch_intel_vbios
    call set_vbe_mode
    mov bx, TSC_VBE_DONE
    call mark_tsc
    jmp enter_pmode

; --- 2. E820 memory map -------------------------------------------------------

read_e820:
    mov di, BOOT_MMAP_ADDR
    xor ebx, ebx                    ; continuation value
    xor bp, bp                      ; entry count
.next:
    mov dword [di + 20], 1          ; ACPI 3 "valid" bit, in case the BIOS returns 20 bytes
    mov eax, 0xe820
    mov ecx, E820_ENTRY_SIZE
    mov edx, 'PAMS'                 ; 'SMAP'
    int 0x15
    jc .end                         ; carry: end of list (or unsupported, if bp == 0)
    cmp eax, 'PAMS'
    jne .end
    jcxz .skip                      ; ignore zero-size replies
    cmp dword [di + 8], 0           ; ignore zero-length regions
    jne .keep
    cmp dword [di + 12], 0
    je .skip
.keep:
    inc bp
    add di, E820_ENTRY_SIZE
    cmp bp, BOOT_MMAP_MAX
    jb .skip
    or dword [BOOT_INFO_ADDR + bi.flags], BI_FLAG_MMAP_TRUNCATED
    jmp .end
.skip:
    test ebx, ebx
    jnz .next
.end:
    test bp, bp
    jz .fail
    movzx eax, bp
    mov [BOOT_INFO_ADDR + bi.mmap_count], eax
    mov dword [BOOT_INFO_ADDR + bi.mmap_addr], BOOT_MMAP_ADDR
    ret
.fail:
    mov si, msg_e820
    jmp fail

; --- 3. A20 -------------------------------------------------------------------

enable_a20:
    call a20_on
    je .ok
    mov ax, 0x2401                  ; BIOS
    int 0x15
    call a20_on
    je .ok
    in al, 0x92                     ; "fast A20" (ICH7 supports it)
    test al, 2
    jnz .check
    or al, 2
    and al, 0xfe                    ; never set bit 0: that resets the machine
    out 0x92, al
.check:
    call a20_on
    je .ok
    mov si, msg_a20
    jmp fail
.ok:
    ret

; ZF=1 if A20 is enabled. Compares 0000:7DFE (stage 1's 0xAA55) with its
; 1 MiB alias FFFF:7E0E, restoring both bytes afterwards.
a20_on:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    not ax
    mov es, ax
    mov si, 0x7dfe
    mov di, 0x7e0e
    mov al, [ds:si]
    mov ah, [es:di]
    push ax
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xff
    cmp byte [ds:si], 0xff          ; changed through the alias -> wrapped -> A20 off
    pop ax
    mov [es:di], ah
    mov [ds:si], al
    pop es
    pop ds
    jne .on
    or al, 1                        ; clear ZF (al|1 is never 0)
    ret
.on:
    cmp al, al                      ; set ZF
    ret

; --- 3b. font -----------------------------------------------------------------

; Ask the video BIOS where its 8x16 font lives, for the kernel's on-screen
; log. Not fatal: without it the kernel just has no text console.
get_bios_font:
    push es
    push bp
    mov ax, 0x1130
    mov bh, 6                       ; 8x16 font
    xor bp, bp
    int 0x10                        ; -> ES:BP font, CX bytes per glyph
    cmp cx, 16
    jne .done
    mov ax, es                      ; physical address = ES * 16 + BP
    movzx eax, ax
    shl eax, 4
    movzx ebx, bp
    add eax, ebx
    jz .done                        ; 0000:0000 means "no font"
    mov [BOOT_INFO_ADDR + bi.font_addr], eax
.done:
    pop bp
    pop es
    ret

; --- 4. kernel ----------------------------------------------------------------

load_kernel:
    ; Header first: one sector to the bounce buffer.
    mov dword [dap.lba], KERNEL_LBA
    mov word [dap.seg], BOUNCE_SEG
    mov cx, 1
    call read_sectors

    push ds
    mov ax, BOUNCE_SEG
    mov ds, ax
    mov esi, [kh.magic]
    mov edi, [kh.version]
    mov eax, [kh.load_addr]
    mov ebx, [kh.entry]
    mov ecx, [kh.file_size]
    mov edx, [kh.mem_end]
    pop ds
    mov [kernel_load], eax
    mov [kernel_entry], ebx
    mov [kernel_size], ecx
    mov [kernel_mem_end], edx

    cmp esi, KERNEL_MAGIC
    jne .bad
    cmp edi, KERNEL_VERSION
    jne .bad
    cmp eax, 0x100000               ; load at or above 1 MiB
    jb .bad
    cmp ecx, kh_size
    jb .bad
    cmp ebx, eax                    ; load <= entry < load + file_size
    jb .bad
    add eax, ecx
    jc .bad
    cmp ebx, eax
    jae .bad
    cmp edx, eax                    ; mem_end >= load + file_size
    jb .bad
    cmp edx, KERNEL_MEM_LIMIT       ; stay well inside the EeePC's 1 GiB
    ja .bad
    add ecx, 511
    shr ecx, 9                      ; sectors
    cmp ecx, KERNEL_MAX_SECTORS
    ja .too_big

    ; Whole image, in 32 KiB chunks (re-reads the header sector; simpler).
    mov dword [dap.lba], KERNEL_LBA
    mov word [dap.seg], BOUNCE_SEG
    mov di, cx                      ; sectors remaining
.chunk:
    mov cx, di
    cmp cx, READ_CHUNK
    jbe .read
    mov cx, READ_CHUNK
.read:
    push cx
    call read_sectors
    pop cx
    movzx eax, cx
    add [dap.lba], eax
    shl cx, 5                       ; sectors * 512 / 16 = paragraphs
    add [dap.seg], cx
    shr cx, 5
    sub di, cx
    jnz .chunk

    mov eax, [kernel_load]
    mov [BOOT_INFO_ADDR + bi.kernel_start], eax
    mov eax, [kernel_mem_end]
    mov [BOOT_INFO_ADDR + bi.kernel_end], eax
    ret
.bad:
    mov si, msg_kernel_bad
    jmp fail
.too_big:
    mov si, msg_kernel_big
    jmp fail

; Read CX sectors from [dap.lba] to [dap.seg]:0. Retries, fails loudly.
read_sectors:
    mov [read_count], cx
    mov bp, READ_TRIES
.try:
    mov cx, [read_count]            ; BIOS may overwrite dap.count on failure
    mov [dap.count], cx
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .ok
    dec bp
    jz .fail
    xor ah, ah                      ; reset the drive, then retry
    mov dl, [boot_drive]
    int 0x13
    jmp .try
.ok:
    ret
.fail:
    mov si, msg_read
    jmp fail

; --- 5a. 1024x600 on the GMA 950 --------------------------------------------
;
; The EeePC's Intel video BIOS has no mode for its own 1024x600 panel. Like the
; Linux tool 915resolution, rewrite one mode it can never show on that panel
; (internal mode 0x5c, 1920x1440 x 32 bpp, whose record 0x3c/0x4d share) to
; 1024x600 in the BIOS's shadow-RAM copy, before the mode walk. The BIOS's
; panel fitter supplies the real timings, so only width/height change.
;
; Runs only when all of these hold, else does nothing:
;   - the host bridge is an Intel 945GM/GSE (or Q35/G33: same PAM registers)
;   - the video BIOS has the Intel mode table (entries 0x30, 0x32, 0x34 ...)
;   - mode 0x5c exists at 32 bpp and its record is wider than 1024 (unused)
; Shadow RAM is unlocked through PAM1/PAM2, patched, read back, and the PAM
; registers restored. Outcome recorded in bi.flags.

PATCH_MODE      equ 0x5c
PATCH_WIDTH     equ 1024
PATCH_HEIGHT    equ 600
PAM_ADDRESS     equ 0x80000090          ; host bridge 00:00.0, PAM0-PAM3
PAM_RW          equ 0x33                ; both halves: reads and writes go to RAM

patch_intel_vbios:
    mov eax, 0x80000000                 ; host bridge vendor/device
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfc
    in eax, dx
    cmp eax, 0x27ac8086                 ; 945GSE/GME (the EeePC 1000HE)
    je .chipset_ok
    cmp eax, 0x27a08086                 ; 945GM
    je .chipset_ok
    cmp eax, 0x29c08086                 ; Q35/G33 (same PAM layout; QEMU q35 for tests)
    jne .skip
.chipset_ok:
    push es
    mov ax, 0xc000
    mov es, ax
    cmp word [es:0], 0xaa55             ; option ROM signature
    jne .done
    movzx cx, byte [es:2]               ; ROM size in 512-byte blocks
    shl cx, 9
    jnz .limit_ok
    mov cx, 0xfff0                      ; 128 blocks = 64 KiB wraps to 0
.limit_ok:
    sub cx, 16
    xor di, di
.search:
    cmp byte [es:di], 0x30
    jne .next_byte
    cmp byte [es:di + 5], 0x32
    jne .next_byte
    cmp byte [es:di + 10], 0x34
    je .found
.next_byte:
    inc di
    cmp di, cx
    jb .search
    jmp .done

.found:
    mov cx, 64                          ; entries to look at, at most
.entry:
    mov al, [es:di]
    cmp al, 0xff                        ; end of table
    je .done
    cmp al, PATCH_MODE
    jne .next_entry
    cmp byte [es:di + 1], 32
    je .got_mode
.next_entry:
    add di, 5
    loop .entry
    jmp .done

.got_mode:
    mov bx, [es:di + 2]                 ; offset of its resolution record
    movzx ax, byte [es:bx + 4]          ; width = (x2 & 0xf0) << 4 | x1
    and al, 0xf0
    shl ax, 4
    or al, [es:bx + 2]
    cmp ax, PATCH_WIDTH
    jbe .done                           ; already usable (or already patched): leave it

    mov eax, PAM_ADDRESS                ; save PAM1 (0x91) and PAM2 (0x92), unlock
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfd
    in al, dx
    mov [saved_pam1], al
    mov al, PAM_RW
    out dx, al
    mov dx, 0xcfe
    in al, dx
    mov [saved_pam2], al
    mov al, PAM_RW
    out dx, al

    mov byte [es:bx + 2], PATCH_WIDTH & 0xff
    mov al, [es:bx + 4]
    and al, 0x0f
    or al, (PATCH_WIDTH >> 4) & 0xf0
    mov [es:bx + 4], al
    mov byte [es:bx + 5], PATCH_HEIGHT & 0xff
    mov al, [es:bx + 7]
    and al, 0x0f
    or al, (PATCH_HEIGHT >> 4) & 0xf0
    mov [es:bx + 7], al

    mov edx, BI_FLAG_VBIOS_PATCHED      ; did it stick? (it won't if PAM ignored us)
    cmp byte [es:bx + 2], PATCH_WIDTH & 0xff
    jne .failed
    cmp byte [es:bx + 5], PATCH_HEIGHT & 0xff
    je .relock
.failed:
    mov edx, BI_FLAG_VBIOS_PATCH_FAILED
.relock:
    or [BOOT_INFO_ADDR + bi.flags], edx
    mov eax, PAM_ADDRESS
    mov dx, 0xcf8
    out dx, eax
    mov dx, 0xcfd
    mov al, [saved_pam1]
    out dx, al
    mov dx, 0xcfe
    mov al, [saved_pam2]
    out dx, al
.done:
    pop es
.skip:
    ret

; --- 5. VBE -------------------------------------------------------------------

; Walks the VBE mode list once, stopping early at 1024x600. Accepts only
; 32 bpp direct-colour modes with a linear framebuffer.
; Preference: 1024x600 (the EeePC panel) > 1024x768 > 800x600.
set_vbe_mode:
    mov di, VBE_INFO
    mov dword [di], 'VBE2'          ; ask for VBE 2.0+ info
    mov ax, 0x4f00
    int 0x10
    cmp ax, 0x004f
    jne .none
    cmp dword [VBE_INFO], 'VESA'
    jne .none
    cmp word [VBE_INFO + 4], 0x0200 ; linear framebuffers need VBE 2.0
    jb .none
    call record_vbe_info

    lfs si, [VBE_INFO + 14]         ; far pointer to the mode list
    mov word [best_mode], 0xffff
    mov byte [best_score], 0
.next:
    mov cx, [fs:si]
    add si, 2
    cmp cx, 0xffff
    je .chosen
    mov [cur_mode], cx              ; don't trust the BIOS to preserve CX
    mov di, VBE_MODE_INFO
    mov ax, 0x4f01
    int 0x10
    cmp ax, 0x004f
    jne .next
    call record_vbe_mode
    mov ax, [VBE_MODE_INFO]         ; attributes: supported, graphics, LFB
    and ax, 0x0091
    cmp ax, 0x0091
    jne .next
    cmp byte [VBE_MODE_INFO + 0x19], 32     ; bpp
    jne .next
    cmp byte [VBE_MODE_INFO + 0x1b], 6      ; memory model: direct colour
    jne .next
    mov ax, [VBE_MODE_INFO + 0x12]  ; width
    mov dx, [VBE_MODE_INFO + 0x14]  ; height
    xor bl, bl
    cmp ax, 800
    jne .not800
    cmp dx, 600
    jne .score
    mov bl, 1
    jmp .score
.not800:
    cmp ax, 1024
    jne .score
    cmp dx, 768
    jne .not768
    mov bl, 2
    jmp .score
.not768:
    cmp dx, 600
    jne .score
    mov bl, 3
.score:
    cmp bl, [best_score]
    jbe .next
    mov [best_score], bl
    mov ax, [cur_mode]
    mov [best_mode], ax
    cmp bl, 3
    jne .next                       ; exact panel match: stop walking

.chosen:
    mov cx, [best_mode]
    cmp cx, 0xffff
    je .none
    mov di, VBE_MODE_INFO           ; re-read the winner's info
    mov ax, 0x4f01
    int 0x10
    cmp ax, 0x004f
    jne .none
    mov bx, [best_mode]             ; not CX: the BIOS needn't preserve it
    or bx, 0x4000                   ; use the linear framebuffer
    mov ax, 0x4f02
    int 0x10
    cmp ax, 0x004f
    jne .none

    mov eax, [VBE_MODE_INFO + 0x28]
    mov [BOOT_INFO_ADDR + bi.fb_addr], eax
    movzx eax, word [VBE_MODE_INFO + 0x10]
    mov [BOOT_INFO_ADDR + bi.fb_pitch], eax
    movzx eax, word [VBE_MODE_INFO + 0x12]
    mov [BOOT_INFO_ADDR + bi.fb_width], eax
    movzx eax, word [VBE_MODE_INFO + 0x14]
    mov [BOOT_INFO_ADDR + bi.fb_height], eax
    movzx eax, byte [VBE_MODE_INFO + 0x19]
    mov [BOOT_INFO_ADDR + bi.fb_bpp], eax
    or dword [BOOT_INFO_ADDR + bi.flags], BI_FLAG_FB
    ret
.none:
    mov si, msg_vbe
    jmp fail

; Record the controller info for the kernel's log: version, video memory and
; the BIOS's name string (copied, since it may live in the scratch buffer).
record_vbe_info:
    movzx eax, word [VBE_INFO + 4]
    mov [BOOT_INFO_ADDR + bi.vbe_version], eax
    movzx eax, word [VBE_INFO + 18] ; in 64 KiB units
    shl eax, 6
    mov [BOOT_INFO_ADDR + bi.vbe_mem_kb], eax
    mov dword [BOOT_INFO_ADDR + bi.vbe_modes_addr], BOOT_VBE_MODES_ADDR
    push ds
    mov di, BOOT_INFO_ADDR + bi.vbe_oem     ; ES = 0; area already zeroed
    mov cx, VBE_OEM_MAX - 1
    lds si, [VBE_INFO + 6]          ; far pointer to the OEM string
.copy:
    lodsb
    test al, al
    jz .done
    stosb
    loop .copy
.done:
    pop ds
    ret

; Append the mode just described in VBE_MODE_INFO to the boot_info mode list.
record_vbe_mode:
    mov eax, [BOOT_INFO_ADDR + bi.vbe_modes_count]
    cmp eax, BOOT_VBE_MODES_MAX
    jae .full
    imul di, ax, VBE_MODE_ENTRY_SIZE
    add di, BOOT_VBE_MODES_ADDR
    mov ax, [cur_mode]
    mov [di + vme.mode], ax
    mov ax, [VBE_MODE_INFO + 0x12]
    mov [di + vme.width], ax
    mov ax, [VBE_MODE_INFO + 0x14]
    mov [di + vme.height], ax
    mov al, [VBE_MODE_INFO + 0x19]
    mov [di + vme.bpp], al
    mov al, [VBE_MODE_INFO + 0x1b]
    mov [di + vme.model], al
    mov ax, [VBE_MODE_INFO]
    mov [di + vme.attributes], ax
    inc dword [BOOT_INFO_ADDR + bi.vbe_modes_count]
.full:
    ret

; --- 6. protected mode --------------------------------------------------------

enter_pmode:
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pmode

bits 32
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7c00

    ; Copy the kernel from the bounce buffer, then zero its bss.
    mov esi, BOUNCE_SEG * 16
    mov edi, [kernel_load]
    mov ecx, [kernel_size]
    add ecx, 3
    shr ecx, 2
    rep movsd
    mov edi, [kernel_load]
    add edi, [kernel_size]
    mov ecx, [kernel_mem_end]
    sub ecx, edi
    xor eax, eax
    rep stosb

    rdtsc
    mov [BOOT_INFO_ADDR + bi.tsc + TSC_KERNEL_ENTRY * 8], eax
    mov [BOOT_INFO_ADDR + bi.tsc + TSC_KERNEL_ENTRY * 8 + 4], edx

    mov eax, BOOT_INFO_MAGIC
    mov ebx, BOOT_INFO_ADDR
    jmp [kernel_entry]

bits 16

; --- helpers ------------------------------------------------------------------

; Store rdtsc into boot_info.tsc[BX].
mark_tsc:
    rdtsc
    shl bx, 3
    mov [BOOT_INFO_ADDR + bi.tsc + bx], eax
    mov [BOOT_INFO_ADDR + bi.tsc + bx + 4], edx
    ret

serial_init:                        ; COM1: 115200 8N1, FIFOs on
    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1
    mov al, 1
    out dx, al
    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1 + 2
    mov al, 0xc7
    out dx, al
    ret

fail:
    push si
    mov si, msg_prefix
    call print
    pop si
    call print
.halt:
    cli
    hlt
    jmp .halt

; Print NUL-terminated DS:SI to the screen and COM1 (bounded UART wait).
print:
    lodsb
    test al, al
    jz .done
    push ax
    mov dx, COM1 + 5
    mov cx, 0x8000
.tx:
    in al, dx
    test al, 0x20
    loopz .tx
    pop ax
    mov dx, COM1
    out dx, al
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

; --- data ---------------------------------------------------------------------

msg_prefix      db "LKX stage2: ", 0
msg_e820        db "E820 memory map unavailable", 13, 10, 0
msg_a20         db "cannot enable A20", 13, 10, 0
msg_read        db "kernel read error", 13, 10, 0
msg_kernel_bad  db "bad kernel header", 13, 10, 0
msg_kernel_big  db "kernel too big", 13, 10, 0
msg_vbe         db "no usable VBE mode (need 32 bpp LFB)", 13, 10, 0

align 8
gdt:
    dq 0                            ; null
    dq 0x00cf9a000000ffff           ; 0x08: code, base 0, 4 GiB, 32-bit, ring 0
    dq 0x00cf92000000ffff           ; 0x10: data, base 0, 4 GiB, 32-bit, ring 0
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

dap:                                ; int 13h AH=42h disk address packet
    db 0x10, 0
.count: dw 0
.off:   dw 0
.seg:   dw 0
.lba:   dq 0

boot_drive      db 0
best_score      db 0
best_mode       dw 0
saved_pam1      db 0
saved_pam2      db 0
cur_mode        dw 0
read_count      dw 0
tsc_stage2      dq 0
kernel_load     dd 0
kernel_entry    dd 0
kernel_size     dd 0
kernel_mem_end  dd 0

; Pad to whole sectors; the kernel starts on the next one.
    times (512 - ($ - $$) % 512) % 512 db 0
stage2_end:
KERNEL_LBA equ 1 + (stage2_end - $$) / 512

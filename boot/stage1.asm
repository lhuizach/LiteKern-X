; LiteKern X — stage 1: MBR boot sector.
;
; Records T0, loads stage 2 with one LBA read, checks it, jumps to it.
; Contract with stage 2: docs/BOOT-PROTOCOL.md.
;
; Build-time defines (the Makefile passes these):
;   STAGE2_SECTORS  size of stage 2 in sectors (1..64)
;   DISK_SECTORS    size of the boot area (MBR + stage 2 + kernel) in sectors
;   FAT_START, FAT_SECTORS   the FAT32 partition (FAT_SECTORS 0: none)

bits 16
org 0x7c00

%ifndef STAGE2_SECTORS
    %error "STAGE2_SECTORS must be defined"
%endif
%ifndef DISK_SECTORS
    %error "DISK_SECTORS must be defined"
%endif
%ifndef FAT_SECTORS
    %define FAT_SECTORS 0
    %define FAT_START 0
%endif
%if STAGE2_SECTORS < 1 || STAGE2_SECTORS > 64
    %error "stage 2 must be 1..64 sectors (it loads into 0x8000-0xFFFF in one read)"
%endif

STAGE2_LBA   equ 1
STAGE2_ADDR  equ 0x8000
STAGE2_MAGIC equ 'LKX2'
BOOT_T0      equ 0x0500
COM1         equ 0x3f8
READ_TRIES   equ 3

    jmp short start
    nop
    ; FAT BPB area. BIOSes emulating a USB stick as a floppy may overwrite
    ; this in memory, so no code or data lives here.
    times 90 - ($ - $$) db 0

start:
    mov bp, dx                          ; rdtsc clobbers EDX: keep the boot drive (DL)
    rdtsc                               ; T0: boot-time budget starts here
    cli
    xor bx, bx
    mov ds, bx
    mov es, bx
    mov ss, bx
    mov sp, 0x7c00
    mov [BOOT_T0], eax
    mov [BOOT_T0 + 4], edx
    jmp 0:.cs_ok                        ; some BIOSes enter at 07C0:0000
.cs_ok:
    sti
    cld
    mov dx, bp
    mov [boot_drive], dl

    ; LBA extensions are required: no CHS fallback.
    mov ah, 0x41
    mov bx, 0x55aa
    int 0x13
    jc .no_lba
    cmp bx, 0xaa55
    jne .no_lba
    test cl, 1                          ; bit 0: DAP-based access supported
    jz .no_lba

    mov di, READ_TRIES
.read:
    mov word [dap.count], STAGE2_SECTORS    ; BIOS may overwrite it on failure
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .loaded
    dec di
    jz .read_fail
    xor ah, ah                          ; reset the drive, then retry
    mov dl, [boot_drive]
    int 0x13
    jmp .read

.loaded:
    cmp dword [STAGE2_ADDR], STAGE2_MAGIC
    jne .bad_stage2
    mov dl, [boot_drive]
    jmp 0:STAGE2_ADDR + 4

.no_lba:
    mov si, msg_no_lba
    jmp short .fail
.read_fail:
    mov si, msg_read
    jmp short .fail
.bad_stage2:
    mov si, msg_bad
.fail:
    push si
    mov si, msg_prefix
    call print
    pop si
    call print
.halt:
    cli
    hlt
    jmp .halt

; Print the NUL-terminated string at DS:SI to the screen and COM1.
; COM1 isn't initialised here: QEMU doesn't need it, and the EeePC has no
; UART (reads return 0xFF, writes are ignored). The wait is bounded so an
; odd port can never hang the boot.
print:
    lodsb
    test al, al
    jz .done
    push ax
    mov dx, COM1 + 5                    ; LSR
    mov cx, 0x8000
.tx:
    in al, dx
    test al, 0x20                       ; transmit holding register empty?
    loopz .tx
    pop ax
    mov dx, COM1
    out dx, al
    mov ah, 0x0e                        ; BIOS teletype
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

msg_prefix db "LKX stage1: ", 0
msg_no_lba db "no LBA", 13, 10, 0
msg_read   db "disk read error", 13, 10, 0
msg_bad    db "bad stage 2", 13, 10, 0

dap:                                    ; int 13h AH=42h disk address packet
    db 0x10, 0
.count:
    dw STAGE2_SECTORS
    dw STAGE2_ADDR, 0                   ; buffer offset, segment
    dq STAGE2_LBA

boot_drive db 0

; --- MBR tail -----------------------------------------------------------------
    times 440 - ($ - $$) db 0           ; assembly fails here if the code is too big
    dd 0x2058_4b4c                      ; disk signature "LKX "
    dw 0

    ; Partition 1: active, type 0x7F, LBA 1 .. end of image. CHS fields are
    ; set to the "use LBA" marker (1023/254/63).
    db 0x80
    db 0xfe, 0xff, 0xff
    db 0x7f
    db 0xfe, 0xff, 0xff
    dd 1
    dd DISK_SECTORS - 1
%if FAT_SECTORS > 0
    ; Partition 2: FAT32 (LBA), for files; Windows can open it too.
    db 0x00
    db 0xfe, 0xff, 0xff
    db 0x0c
    db 0xfe, 0xff, 0xff
    dd FAT_START
    dd FAT_SECTORS
    times 2 * 16 db 0                   ; partitions 3-4 unused
%else
    times 3 * 16 db 0                   ; partitions 2-4 unused
%endif

    dw 0xaa55

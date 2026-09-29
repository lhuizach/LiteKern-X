; LiteKern X — boot protocol constants and structures (assembly side).
; Must match boot/bootinfo.h exactly. Documented in docs/BOOT-PROTOCOL.md.

BOOT_T0_ADDR        equ 0x0500      ; qword, written by stage 1
BOOT_INFO_ADDR      equ 0x0600
BOOT_MMAP_ADDR      equ 0x0700
BOOT_MMAP_MAX       equ 32
E820_ENTRY_SIZE     equ 24

BOOT_INFO_MAGIC     equ 'LKXB'
BOOT_INFO_VERSION   equ 3           ; 2: adds font_addr; 3: adds the VBE report
BOOT_VBE_MODES_ADDR equ 0x0A00      ; VBE mode list, BOOT_VBE_MODES_MAX x VBE_MODE_ENTRY_SIZE
BOOT_VBE_MODES_MAX  equ 64
VBE_MODE_ENTRY_SIZE equ 12
VBE_OEM_MAX         equ 48
KERNEL_MAGIC        equ 'LKXK'
KERNEL_VERSION      equ 1

; bi.flags
BI_FLAG_FB              equ 1 << 0  ; framebuffer fields valid
BI_FLAG_MMAP_TRUNCATED  equ 1 << 1  ; BIOS reported more than BOOT_MMAP_MAX entries
BI_FLAG_VBIOS_PATCHED   equ 1 << 2  ; Intel video BIOS mode 0x5c rewritten to 1024x600
BI_FLAG_VBIOS_PATCH_FAILED equ 1 << 3 ; tried, but the write didn't stick (shadow RAM locked)

; bi.tsc[] indices
TSC_STAGE1          equ 0   ; T0: stage 1 entry
TSC_STAGE2          equ 1   ; stage 2 entry
TSC_KERNEL_LOADED   equ 2   ; kernel read from disk  -> end of "bootloader" phase
TSC_VBE_DONE        equ 3   ; VBE mode set           -> end of "vbe" phase
TSC_KERNEL_ENTRY    equ 4   ; about to jump to the kernel
TSC_COUNT           equ 5

struc bi
    .magic          resd 1
    .version        resd 1
    .boot_drive     resd 1
    .flags          resd 1
    .tsc            resq TSC_COUNT
    .mmap_addr      resd 1
    .mmap_count     resd 1
    .fb_addr        resd 1
    .fb_pitch       resd 1
    .fb_width       resd 1
    .fb_height      resd 1
    .fb_bpp         resd 1
    .kernel_start   resd 1
    .kernel_end     resd 1
    .font_addr      resd 1          ; video BIOS 8x16 font (256 glyphs x 16 bytes), 0 if none
    .vbe_version    resd 1          ; BCD, e.g. 0x0300 = VBE 3.0
    .vbe_mem_kb     resd 1          ; video memory the BIOS reports
    .vbe_modes_addr resd 1          ; struct vbe_mode_entry[vbe_modes_count]
    .vbe_modes_count resd 1
    .vbe_oem        resb VBE_OEM_MAX ; BIOS name string, NUL-terminated, truncated
endstruc

; One VBE mode as the BIOS described it (every mode it answered for, until the
; walk stopped at a 1024x600 match or the list ended).
struc vme
    .mode           resw 1
    .width          resw 1
    .height         resw 1
    .bpp            resb 1
    .model          resb 1          ; 4 packed pixel, 6 direct colour
    .attributes     resw 1          ; bit 0 supported, 4 graphics, 7 linear framebuffer
    .reserved       resw 1
endstruc

; Kernel image header: the first bytes of the kernel image on disk.
struc kh
    .magic          resd 1          ; KERNEL_MAGIC
    .version        resd 1          ; KERNEL_VERSION
    .load_addr      resd 1          ; physical load address, >= 1 MiB
    .entry          resd 1          ; physical entry point
    .file_size      resd 1          ; bytes in the image, header included
    .mem_end        resd 1          ; end of the kernel in memory (bss zeroed up to here)
endstruc

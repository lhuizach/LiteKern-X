; LiteKern X — the real-mode BIOS thunk (boot/bios_thunk.asm, assembled as a
; flat binary), carried in the kernel image; drivers/bios_disk.c copies it to
; where it runs. BIOS_THUNK_FILE is passed by the Makefile.

section .rodata
global bios_thunk_blob, bios_thunk_blob_size

bios_thunk_blob:
    incbin BIOS_THUNK_FILE
bios_thunk_end:

align 4
bios_thunk_blob_size:
    dd bios_thunk_end - bios_thunk_blob

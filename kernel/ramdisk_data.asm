; LiteKern X — the ramdisk (tools/mkramdisk.py), built into the kernel image
; (read-only). RAMDISK_FILE is passed by the Makefile.

section .rodata
align 4
global ramdisk_data, ramdisk_size

ramdisk_data:
    incbin RAMDISK_FILE
ramdisk_end:

align 4
ramdisk_size:
    dd ramdisk_end - ramdisk_data

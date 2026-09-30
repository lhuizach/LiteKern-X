; LiteKern X — the packed wallpaper, built into the kernel image (read-only).
; WALLPAPER_FILE is passed by the Makefile: $(BUILD)/gen/wallpaper.lkxw, made
; by tools/wallpaper-pack.py (see kernel/wallpaper.h).

section .rodata
align 4
global wallpaper_data, wallpaper_size

wallpaper_data:
    incbin WALLPAPER_FILE
wallpaper_end:

align 4
wallpaper_size:
    dd wallpaper_end - wallpaper_data

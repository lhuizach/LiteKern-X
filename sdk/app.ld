/* LiteKern X — how a KERN86 app is laid out (sdk/, Phase 2 §5).
 *
 * The 32-byte .lkx header sits just below K86_APP_BASE (0x80000000), so
 * `objcopy -O binary` writes header + image as one file. The loader copies
 * the image to K86_APP_BASE and maps __bss_size zeroed bytes after it: the
 * .bss is part of the image's memory, not the file. */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
    . = 0x80000000 - 32;
    .lkxhdr : { KEEP(*(.lkxhdr)) }
    . = 0x80000000;
    .text   : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    __image_end = .;
    . = ALIGN(4);
    __bss_start = .;
    .bss    : { *(.bss*) *(COMMON) }
    __bss_end = .;

    __image_size = __image_end - 0x80000000;
    __bss_size = __bss_end - __image_end;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}

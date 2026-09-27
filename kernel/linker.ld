/* LiteKern X — kernel link layout. Loaded by stage 2 at 1 MiB as a flat binary
 * (objcopy -O binary); the .header section must come first. */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    __kernel_start = .;

    .text : {
        KEEP(*(.header))
        *(.text .text.*)
    }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data : ALIGN(16) { *(.data .data.*) }

    /* Everything above goes into kernel.bin; the build checks the size matches. */
    __kernel_file_end = .;

    .bss : ALIGN(16) {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }
    __kernel_end = .;

    __kernel_file_size = __kernel_file_end - __kernel_start;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
    }
}

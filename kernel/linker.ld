/* LiteKern X — kernel link layout. Loaded by stage 2 at 1 MiB as a flat binary
 * (objcopy -O binary); the .header section must come first.
 *
 * Code and read-only data end on a page boundary (__ro_end) so the kernel can
 * map them read-only once paging is on (kernel/vmm.c). */
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
    . = ALIGN(4096);
    __ro_end = .;

    /* LONG(0): .data always has content, so objcopy's output runs exactly to
     * __kernel_file_end even when the kernel has no initialised data. */
    .data : { *(.data .data.*) LONG(0) }

    /* Everything above goes into kernel.bin; the build checks the size matches. */
    __kernel_file_end = .;

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }
    __kernel_end = ALIGN(4096);

    __kernel_file_size = __kernel_file_end - __kernel_start;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
    }
}

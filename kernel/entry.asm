; LiteKern X — kernel entry. Stage 2 jumps to _start (docs/BOOT-PROTOCOL.md):
; 32-bit protected mode, interrupts off, EAX = 'LKXB', EBX = &boot_info.

bits 32

%include "boot/bootinfo.inc"

extern __kernel_start, __kernel_file_size, __kernel_end
extern kmain
global _start

KERNEL_STACK_SIZE equ 16 * 1024

; Kernel image header. The linker script puts this section first.
section .header progbits alloc noexec nowrite align=4
    dd KERNEL_MAGIC
    dd KERNEL_VERSION
    dd __kernel_start               ; load address
    dd _start                       ; entry
    dd __kernel_file_size
    dd __kernel_end                 ; stage 2 zeroes bss up to here

section .text
_start:
    cld
    mov esp, stack_top              ; leave the bootloader's small low-memory stack
    xor ebp, ebp                    ; terminates stack traces
    push ebx                        ; struct boot_info *
    push eax                        ; magic
    call kmain                      ; never returns
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb KERNEL_STACK_SIZE
stack_top:

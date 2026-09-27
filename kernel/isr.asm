; LiteKern X — interrupt entry stubs: CPU exceptions (vectors 0-31), hardware
; IRQs (32-47) and the system call gate (int 0x80). Each stub builds a uniform
; frame (struct int_frame in kernel/idt.h) and calls interrupt_handler().

bits 32

extern interrupt_handler
global isr_stubs, isr_syscall

KERNEL_DS equ 0x10

%macro ISR_NOERR 1
isr%1:
    push 0                          ; fake error code keeps the frame uniform
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:
    push %1                         ; CPU already pushed the error code
    jmp isr_common
%endmacro

section .text

%assign v 0
%rep 48
    %if v == 8 || v == 10 || v == 11 || v == 12 || v == 13 || v == 14 || v == 17 || v == 21 || v == 29 || v == 30
        ISR_ERR v
    %else
        ISR_NOERR v
    %endif
    %assign v v + 1
%endrep

isr_syscall:
    push 0
    push 0x80
    jmp isr_common

isr_common:
    pushad
    push ds                         ; ring 3 arrives with user data segments:
    push es                         ; save them and switch to the kernel's
    push fs
    push gs
    mov ax, KERNEL_DS
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp                        ; struct int_frame *
    call interrupt_handler
    add esp, 4
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8                      ; vector + error code
    iretd

section .rodata
align 4
isr_stubs:
%assign v 0
%rep 48
    dd isr%+v
    %assign v v + 1
%endrep

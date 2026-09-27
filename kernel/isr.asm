; LiteKern X — CPU exception entry stubs (vectors 0-31).
; Each stub pushes a uniform frame (see struct int_frame in kernel/idt.h) and
; calls exception_handler().

bits 32

extern exception_handler
global isr_stubs

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
%rep 32
    %if v == 8 || v == 10 || v == 11 || v == 12 || v == 13 || v == 14 || v == 17 || v == 21 || v == 29 || v == 30
        ISR_ERR v
    %else
        ISR_NOERR v
    %endif
    %assign v v + 1
%endrep

isr_common:
    pushad
    push esp                        ; struct int_frame *
    call exception_handler
    add esp, 4
    popad
    add esp, 8                      ; vector + error code
    iretd

section .rodata
align 4
isr_stubs:
%assign v 0
%rep 32
    dd isr%+v
    %assign v v + 1
%endrep

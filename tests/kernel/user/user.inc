; LiteKern X — shared header for the ring 3 test programs in this directory.
; Each program is a flat binary loaded at USER_BASE and entered at its first
; byte. Syscall numbers must match kernel/syscall.h.

bits 32
org 0x80000000

SYS_EXIT        equ 0
SYS_DEBUG_WRITE equ 1
SYS_UPTIME_MS   equ 2

EFAULT          equ 14
ENOSYS          equ 38

%macro EXIT 1
    mov ebx, %1
    mov eax, SYS_EXIT
    int 0x80
%endmacro

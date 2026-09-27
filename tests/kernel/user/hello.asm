; Makes a syscall that returns, then exits with 42.
%include "tests/kernel/user/user.inc"

    mov eax, SYS_DEBUG_WRITE
    mov ebx, msg
    mov ecx, msg_len
    int 0x80
    cmp eax, msg_len
    jne .bad
    EXIT 42
.bad:
    EXIT 1

msg     db "hello from ring 3"
msg_len equ $ - msg

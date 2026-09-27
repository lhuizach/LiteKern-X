; Calls a syscall that doesn't exist and exits with what came back (-ENOSYS).
%include "tests/kernel/user/user.inc"

    mov eax, 999
    int 0x80
    EXIT eax

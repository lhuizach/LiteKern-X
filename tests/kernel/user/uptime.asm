; Exits with the uptime_ms syscall's result.
%include "tests/kernel/user/user.inc"

    mov eax, SYS_UPTIME_MS
    int 0x80
    EXIT eax

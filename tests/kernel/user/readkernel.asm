; Reads kernel memory directly. Must be killed with a page fault.
%include "tests/kernel/user/user.inc"

    mov eax, [0x00100000]
    EXIT 0

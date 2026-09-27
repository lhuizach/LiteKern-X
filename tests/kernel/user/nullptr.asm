; Writes through a null pointer. Must be killed with a page fault.
%include "tests/kernel/user/user.inc"

    mov dword [0], 1
    EXIT 0

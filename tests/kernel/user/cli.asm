; Tries to disable interrupts. Privileged in ring 3 (IOPL 0): must be killed
; with a general protection fault.
%include "tests/kernel/user/user.inc"

    cli
    EXIT 0

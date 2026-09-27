; Touches a hardware port (the i8042). Ring 3 has no I/O permission, so this
; must be killed with a general protection fault.
%include "tests/kernel/user/user.inc"

    in al, 0x64
    EXIT 0

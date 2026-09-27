; LiteKern X — embeds the ring 3 test programs (tests/kernel/user/*.asm,
; assembled to flat binaries by the Makefile) into the self-test kernel.
; USER_BIN_DIR is passed by the Makefile.

%macro PROGRAM 1
    %defstr name %1
    %strcat path USER_BIN_DIR, "/", name, ".user.bin"
    global user_prog_%1, user_prog_%1_end
user_prog_%1:
    incbin path
user_prog_%1_end:
%endmacro

section .rodata
    PROGRAM hello
    PROGRAM badptr
    PROGRAM nosys
    PROGRAM uptime
    PROGRAM exit7
    PROGRAM readkernel
    PROGRAM nullptr
    PROGRAM port_io
    PROGRAM cli

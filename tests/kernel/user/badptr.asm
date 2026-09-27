; Hands the kernel pointers it must refuse. Exits 0 if every one came back
; -EFAULT, otherwise with the number of the first check that didn't.
%include "tests/kernel/user/user.inc"

%macro EXPECT_EFAULT 3          ; check number, buf, len
    mov eax, SYS_DEBUG_WRITE
    mov ebx, %2
    mov ecx, %3
    int 0x80
    cmp eax, -EFAULT
    jne fail_%1
%endmacro

    EXPECT_EFAULT 1, 0x00100000, 16         ; kernel memory
    EXPECT_EFAULT 2, 0x00000000, 4          ; null
    EXPECT_EFAULT 3, 0x80100000, 4          ; user space, but nothing mapped there
    EXPECT_EFAULT 4, 0xbffff000, 4          ; the guard page above the stack
    EXPECT_EFAULT 5, 0xfffffff0, 32         ; wraps around 4 GiB
    EXPECT_EFAULT 6, 0x7ffffffc, 8          ; straddles the start of user space
    EXIT 0

%assign i 1
%rep 6
fail_%+i:
    EXIT i
%assign i i + 1
%endrep

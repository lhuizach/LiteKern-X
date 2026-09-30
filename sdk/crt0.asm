; LiteKern X — the start of every KERN86 app (sdk/).
;
; The .lkx header (kernel/kern86_abi.h) comes first in the file; the linker
; script (sdk/app.ld) places it just below K86_APP_BASE and fills in the
; sizes. Then _start: call main(), and exit with what it returns.

bits 32

extern main
extern __image_size, __bss_size
global _start

section .lkxhdr
    dd 0x41584b4c                   ; "LKXA"
    dd 1                            ; version
    dd __image_size
    dd __bss_size
    dd _start
    dd 0, 0, 0

section .text.start
_start:
    call main
    mov ebx, eax                    ; exit(main())
    xor eax, eax
    int 0x80
.hang:
    jmp .hang

; LiteKern X — entering and leaving ring 3 (Phase 1 §5).
;
;   int user_run(uint32_t entry, uint32_t user_esp)
;       Saves the kernel's callee-saved state, drops to ring 3 at `entry` with
;       the given stack, and returns only when user_return() is called
;       (from the exit syscall, or when the program is killed by a fault).
;
;   void user_return(int code)   -- noreturn; ring 0 only
;       Abandons the trap stack and resumes user_run's caller with `code`.
;
; Traps from ring 3 run on trap_stack (TSS.esp0), never on the kernel stack
; user_run was called from, so a trap can't overwrite live kernel frames.

bits 32

global user_run, user_return, trap_stack_top

USER_CS equ 0x18 | 3
USER_DS equ 0x20 | 3
KERNEL_DS equ 0x10

section .text

user_run:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_esp], esp
    mov eax, [esp + 20]             ; entry
    mov ecx, [esp + 24]             ; user stack

    mov dx, USER_DS
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    push USER_DS                    ; ss
    push ecx                        ; esp
    push 0x002                      ; eflags: IF off (no IRQs yet), IOPL 0
    push USER_CS                    ; cs
    push eax                        ; eip
    xor eax, eax                    ; don't leak kernel values into ring 3
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    iretd

user_return:
    mov eax, [esp + 4]              ; code
    mov esp, [saved_esp]
    mov dx, KERNEL_DS
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .bss
align 16
saved_esp:
    resd 1
trap_stack:
    resb 16 * 1024
trap_stack_top:

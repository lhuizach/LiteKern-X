; LiteKern X — entering and leaving ring 3 (Phase 1 §5; resumable since
; Phase 3 §6).
;
;   int user_enter(const struct int_frame *f)
;       Saves the kernel's callee-saved state and continues the program from
;       the registers in *f (a new program: its entry point; a yielded one:
;       just after its system call). Returns only when user_return() is
;       called (exit, a fault, or a yield).
;
;   void user_return(int code)   -- noreturn; ring 0 only
;       Abandons the trap stack and resumes user_enter's caller with `code`.
;
; Traps from ring 3 run on trap_stack (TSS.esp0), never on the kernel stack
; user_enter was called from, so a trap can't overwrite live kernel frames.
; Nothing is left on the trap stack between runs, so one serves every program.

bits 32

global user_enter, user_return, trap_stack_top

KERNEL_DS equ 0x10

section .text

user_enter:
    push ebp
    push ebx
    push esi
    push edi
    mov [saved_esp], esp
    mov eax, [esp + 20]             ; the frame (kernel memory)
    cli                             ; iretd turns them back on (eflags.IF)
    mov esp, eax                    ; pop the frame as isr_common's exit does
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8                      ; vector + error code
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
    resb 32 * 1024                  ; syscalls run FAT32 and the compositor on it
trap_stack_top:

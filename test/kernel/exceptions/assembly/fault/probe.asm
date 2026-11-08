option casemap:none
; K1: base of the main stack (X86_KERNEL_MAIN_BASE in layout.h terms), exported by stack/main.c.
EXTERN x86_cpu_fault_stack:QWORD
.code

; ====== eccezioni vere senza INT nei vettori riservati alla CPU ===== ;

PUBLIC x86_cpu_fault
x86_cpu_fault PROC
    cmp ecx, 1
    je invalid_opcode
    cmp ecx, 2
    je invalid_selector
    cmp ecx, 4
    je invalid_stack
    xor edx, edx
    mov eax, 1
    xor ecx, ecx
    div ecx
    ret
invalid_opcode:
    ud2
    ret
invalid_selector:
    mov ax, 38h
    mov ds, ax
    ret
invalid_stack:
    mov rsp, x86_cpu_fault_stack
    push rax
    ; Se il push non fallisce, UD2 segnala un fault diverso da quello atteso.
    ud2
x86_cpu_fault ENDP
END

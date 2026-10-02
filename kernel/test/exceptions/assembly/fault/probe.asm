option casemap:none
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
    mov ax, 28h
    mov ds, ax
    ret
invalid_stack:
    mov rsp, 700000001000h
    push rax
    ; Se il push non fallisce, UD2 segnala un fault diverso da quello atteso.
    ud2
x86_cpu_fault ENDP
END

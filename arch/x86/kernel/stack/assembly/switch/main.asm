option casemap:none
EXTERN x86_kernel_stop:PROC
.code

; ====== RSP osservato senza leggere memoria sul vecchio stack ===== ;

PUBLIC x86_kernel_stack_pointer
x86_kernel_stack_pointer PROC
    mov rax, rsp
    ret
x86_kernel_stack_pointer ENDP

; ====== cambio definitivo con shadow space e ingresso C a RSP modulo 16 uguale 8 ===== ;

PUBLIC x86_kernel_stack_switch
x86_kernel_stack_switch PROC
    mov rsp, rcx
    xor ebp, ebp
    sub rsp, 32
    call rdx
    ; Un ritorno inatteso resta sul nuovo stack e viene fermato qui.
    mov rcx, 4b53545245540001h
    call x86_kernel_stop
x86_kernel_stack_switch ENDP
END

; Intel SDM 093 Vol.2A ch.3 FXSAVE/FXRSTOR, pp.3-409..419, Table3-48:
; REX.W format, 512 bytes, aligned16; preserves full x87/XMM/MXCSR image.
; Microsoft x64 ABI: RBX/RBP/RDI/RSI/R12-R15 and XMM6-XMM15 nonvolatile.
; Offset80 is OUR context ABI (592 bytes), not a hardware register offset.
; Raw private backend: caller checks owners/ranges/IF/DF/CPU/live stacks first.
option casemap:none
EXTERN x86_scheduler_task_start:PROC
.code

; ====== x86_scheduler_context_switch ======

x86_scheduler_context_switch PROC
    mov [rcx], rsp
    mov [rcx+8], rbx
    mov [rcx+16], rbp
    mov [rcx+24], rdi
    mov [rcx+32], rsi
    mov [rcx+40], r12
    mov [rcx+48], r13
    mov [rcx+56], r14
    mov [rcx+64], r15
    fxsave64 [rcx+80]
    fxrstor64 [rdx+80]
    mov rbx, [rdx+8]
    mov rbp, [rdx+16]
    mov rdi, [rdx+24]
    mov rsi, [rdx+32]
    mov r12, [rdx+40]
    mov r13, [rdx+48]
    mov r14, [rdx+56]
    mov r15, [rdx+64]
    mov rsp, [rdx]
    ret
x86_scheduler_context_switch ENDP

; ====== x86_scheduler_task_bootstrap ======

; Synthetic RSP=Top-40 -> RET -> Top-32, aligned16 with 32-byte shadow space.
; CALL enters C at Top-40. C must EXIT to dispatcher rather than return here.
x86_scheduler_task_bootstrap PROC
    call x86_scheduler_task_start
    ud2
x86_scheduler_task_bootstrap ENDP
END

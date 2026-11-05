option casemap:none
EXTERN x86_irq_dispatch:PROC
EXTERN x86_irq_after:PROC
.code

; ====== vettore esplicito senza error code CPU ===== ;

IRQ_ENTRY MACRO vector
x86_irq_vector_&vector LABEL BYTE
    push vector
    jmp NEAR PTR irq_common
ENDM
PUBLIC x86_irq_begin
x86_irq_begin LABEL BYTE
irq_vector = 32
REPT 224
    IRQ_ENTRY %irq_vector
    irq_vector = irq_vector + 1
ENDM

; ====== GPR e FXSAVE64 preservati prima di chiamare codice C ===== ;

irq_common LABEL BYTE
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    lea rcx, [r12+120]
    and rsp, -16
    sub rsp, 544
    fxsave64 [rsp+32]
    cld
    cmp QWORD PTR [r12+120], 0f0h
    jne irq_call
    ; Il solo vettore self-test forza una modifica SIMD da ripristinare.
    pxor xmm0, xmm0
    pxor xmm15, xmm15
irq_call:
    call x86_irq_dispatch
    lea rcx, [r12+120]
    call x86_irq_after
    fxrstor64 [rsp+32]
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 8
    iretq
PUBLIC x86_irq_end
x86_irq_end LABEL BYTE

; ====== FXSAVE disponibile senza lazy FPU e senza OSXSAVE ===== ;

PUBLIC x86_irq_cpu_state
x86_irq_cpu_state PROC
    push rbx
    mov eax, 1
    cpuid
    and edx, 7000000h
    cmp edx, 7000000h
    jne cpu_bad
    mov rax, cr0
    test eax, 0ch
    jnz cpu_bad
    mov rax, cr4
    test eax, 40000h
    jnz cpu_bad
    and eax, 600h
    cmp eax, 600h
    jne cpu_bad
    mov eax, 1
    pop rbx
    ret
cpu_bad:
    xor eax, eax
    pop rbx
    ret
x86_irq_cpu_state ENDP

; ====== indirizzi dei vettori esterni in memoria non eseguibile ===== ;

IRQ_TARGET MACRO vector
    DQ x86_irq_vector_&vector
ENDM
.const
ALIGN 8
PUBLIC x86_irq_targets
x86_irq_targets LABEL QWORD
irq_vector = 32
REPT 224
    IRQ_TARGET %irq_vector
    irq_vector = irq_vector + 1
ENDM
END

option casemap:none
.code

; ====== prova INT preservando prima i registri non volatili del chiamante ===== ;

PUBLIC x86_irq_return_test
x86_irq_return_test PROC
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 104
    movdqu XMMWORD PTR [rsp+32], xmm15
    mov [rsp+64], rsp
    pcmpeqd xmm0, xmm0
    pcmpeqd xmm15, xmm15
    mov eax, 101h
    mov ecx, 102h
    mov edx, 103h
    mov ebx, 104h
    mov ebp, 105h
    mov esi, 106h
    mov edi, 107h
    mov r8d, 108h
    mov r9d, 109h
    mov r10d, 10ah
    mov r11d, 10bh
    mov r12d, 10ch
    mov r13d, 10dh
    mov r14d, 10eh
    mov r15d, 10fh
    std
    int 0f0h

; ====== confronto dei quindici GPR e dello stack originale ===== ;

    cmp rax, 101h
    jne registers_bad
    cmp rcx, 102h
    jne registers_bad
    cmp rdx, 103h
    jne registers_bad
    cmp rbx, 104h
    jne registers_bad
    cmp rbp, 105h
    jne registers_bad
    cmp rsi, 106h
    jne registers_bad
    cmp rdi, 107h
    jne registers_bad
    cmp r8, 108h
    jne registers_bad
    cmp r9, 109h
    jne registers_bad
    cmp r10, 10ah
    jne registers_bad
    cmp r11, 10bh
    jne registers_bad
    cmp r12, 10ch
    jne registers_bad
    cmp r13, 10dh
    jne registers_bad
    cmp r14, 10eh
    jne registers_bad
    cmp r15, 10fh
    jne registers_bad
    cmp rsp, [rsp+64]
    jne registers_bad
    pushfq
    pop rax
    and eax, 600h
    cmp eax, 400h
    jne registers_bad
    pcmpeqd xmm1, xmm1
    pcmpeqb xmm0, xmm1
    pmovmskb eax, xmm0
    cmp eax, 65535
    jne registers_bad
    pcmpeqb xmm15, xmm1
    pmovmskb eax, xmm15
    cmp eax, 65535
    jne registers_bad
    mov eax, 1
    jmp registers_restore
registers_bad:
    xor eax, eax

; ====== ABI ripristinata DF zero prima del ritorno al C ===== ;

registers_restore:
    cld
    movdqu xmm15, XMMWORD PTR [rsp+32]
    add rsp, 104
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
x86_irq_return_test ENDP

; ====== spurious software senza origine LAPIC e senza EOI ===== ;

PUBLIC x86_irq_spurious_test
x86_irq_spurious_test PROC
    int 0ffh
    mov eax, 1
    ret
x86_irq_spurious_test ENDP
END

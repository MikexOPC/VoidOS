option casemap:none
EXTERN k5_owner:QWORD
EXTERN k5_negative:QWORD
EXTERN k5_nmi_count:QWORD
EXTERN k5_fx_clean:BYTE
EXTERN k5_sys_window:QWORD
EXTERN k5_sys_budget:QWORD
EXTERN k5_sys_stack:QWORD
EXTERN k5_sys_user_rsp:QWORD
EXTERN x86_k5_stop:PROC
EXTERN x86_k5_sys_dispatch:PROC

.code

; ====== ingresso SYSCALL con RSP utente non attendibile ======

; Intel SDM 2B, SYSCALL: RCX = RIP successivo, R11 = RFLAGS, RSP non salvato.
; Intel SDM 3A §6.8.8: CS e SS da IA32_STAR, RIP da IA32_LSTAR, flag da IA32_FMASK.
; Intel SDM 2B, SWAPGS: scambio fra IA32_GS_BASE e IA32_KERNEL_GS_BASE (C0000102h).
PUBLIC x86_k5_syscall, x86_k5_sys_wait_first, x86_k5_sys_wait_last
x86_k5_syscall PROC
    cmp QWORD PTR [k5_sys_window], 0
    je sys_swap
x86_k5_sys_wait_first LABEL BYTE
    pause
    cmp QWORD PTR [k5_nmi_count], 1
    je sys_window_done
    dec QWORD PTR [k5_sys_budget]
    jnz NEAR PTR x86_k5_sys_wait_first
x86_k5_sys_wait_last LABEL BYTE
    jmp x86_k5_stop
sys_window_done:
    mov QWORD PTR [k5_sys_window], 0
sys_swap:
    cmp QWORD PTR [k5_negative], 14
    je sys_stack
    swapgs
sys_stack:
    lfence
    mov [k5_sys_user_rsp], rsp
    mov rsp, [k5_sys_stack]
    push 2bh
    push QWORD PTR [k5_sys_user_rsp]
    push r11
    push 33h
    push rcx
    push rax
    push rbx
    push rcx
    push rdx
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
    sub rsp, 512
    fxsave64 [rsp]
    fxrstor64 [k5_fx_clean]
    cld
    mov ecx, 0c0000101h
    rdmsr
    shl rdx, 32
    or rax, rdx
    cmp rax, QWORD PTR [k5_owner]
    jne x86_k5_stop
    mov rax, QWORD PTR gs:[0]
    cmp rax, QWORD PTR [k5_owner]
    jne x86_k5_stop
    mov rbx, rsp
    mov rcx, rsp
    sub rsp, 32
    call x86_k5_sys_dispatch
    mov rsp, rbx
    cmp eax, 1
    jne x86_k5_stop
    fxrstor64 [rsp]
    add rsp, 512
    swapgs
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
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
x86_k5_syscall ENDP

; ====== payload syscall copiato in una pagina utente RX ======

PUBLIC x86_k5_sys_user, x86_k5_sys_user_end, x86_k5_sys_loop_first, x86_k5_sys_loop_last
PUBLIC x86_k5_sys_nosce, x86_k5_sys_ok
x86_k5_sys_user LABEL BYTE
    cmp esi, 2
    je NEAR PTR x86_k5_sys_nosce
    cmp esi, 3
    je sys_user_tf
    cmp esi, 4
    je sys_user_rsp
    mov r8, 1111222233334444h
    mov r9, 5555666677778888h
    mov r10, 9999aaaabbbbcccch
    mov r12, 5a5a1234c3c35678h
    mov r13, 0a5a5edcb3c3ca987h
    mov r14, 0123456789abcdefh
    mov r15, 0fedcba9876543210h
    mov rbp, 0f0e0d0c0b0a09080h
    mov rbx, 7071727374757677h
    mov rdx, 0102030405060708h
    ; Vol. 1, MXCSR e FCW: stato non predefinito da preservare.
    mov DWORD PTR [rsp-4], 9f80h
    ldmxcsr DWORD PTR [rsp-4]
    mov WORD PTR [rsp-8], 27fh
    fldcw WORD PTR [rsp-8]
    movq xmm0, r12
    movq xmm1, r13
    punpcklqdq xmm0, xmm1
    xor eax, eax
    mov edi, 41h
    syscall
    cmp rax, 42h
    jne NEAR PTR x86_k5_sys_loop_last
    cmp rdi, 41h
    jne NEAR PTR x86_k5_sys_loop_last
    mov eax, 1
    syscall
    test rax, rax
    jnz NEAR PTR x86_k5_sys_loop_last
    mov eax, 99
    syscall
    cmp rax, -1
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 1111222233334444h
    cmp r8, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 5555666677778888h
    cmp r9, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 9999aaaabbbbcccch
    cmp r10, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 5a5a1234c3c35678h
    cmp r12, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 0a5a5edcb3c3ca987h
    cmp r13, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 0123456789abcdefh
    cmp r14, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 0fedcba9876543210h
    cmp r15, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 0f0e0d0c0b0a09080h
    cmp rbp, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 7071727374757677h
    cmp rbx, rax
    jne NEAR PTR x86_k5_sys_loop_last
    mov rax, 0102030405060708h
    cmp rdx, rax
    jne NEAR PTR x86_k5_sys_loop_last
    movq rdx, xmm0
    cmp rdx, r12
    jne NEAR PTR x86_k5_sys_loop_last
    psrldq xmm0, 8
    movq rdx, xmm0
    cmp rdx, r13
    jne NEAR PTR x86_k5_sys_loop_last
    stmxcsr DWORD PTR [rsp-4]
    cmp DWORD PTR [rsp-4], 9f80h
    jne NEAR PTR x86_k5_sys_loop_last
    fnstcw WORD PTR [rsp-8]
    cmp WORD PTR [rsp-8], 27fh
    jne NEAR PTR x86_k5_sys_loop_last
    xor eax, eax
    xor ebx, ebx
    mov ecx, 4000000h
x86_k5_sys_loop_first LABEL BYTE
    pause
    test ebx, ebx
    jz sys_loop_next
    test esi, esi
    jz NEAR PTR x86_k5_sys_ok
    test eax, eax
    jnz NEAR PTR x86_k5_sys_ok
sys_loop_next:
    dec ecx
    jnz NEAR PTR x86_k5_sys_loop_first
x86_k5_sys_loop_last LABEL BYTE
    ud2
x86_k5_sys_ok LABEL BYTE
    ud2
x86_k5_sys_nosce LABEL BYTE
    syscall
    ud2
sys_user_tf:
    pushfq
    or QWORD PTR [rsp], 100h
    popfq
    xor eax, eax
    syscall
    nop
    ud2
sys_user_rsp:
    mov rsp, 0ffffffff80000000h
    xor eax, eax
    syscall
    ud2
x86_k5_sys_user_end LABEL BYTE
END

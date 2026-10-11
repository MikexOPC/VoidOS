; Test-only Microsoft x64 ABI probe. Real GPR/XMM/control patterns across switch.
; Intel SDM093 Vol.2A FXSAVE64/FXRSTOR64, FLDCW/FNSTCW, LDMXCSR/STMXCSR;
; Vol.1 §8.1.5/§10.2.3: all exceptions masked, only rounding controls changed.
option casemap:none
EXTERN x86_scheduler_context_switch:PROC
.code

; ====== set_xmm_pattern ======

SET_XMM MACRO reg, number
    mov rax, [rsp+560]
    add rax, number
    movq reg, rax
    add rax, 100h
    movq xmm0, rax
    punpcklqdq reg, xmm0
ENDM

; ====== check_xmm_pattern ======

CHECK_XMM MACRO reg, number
    mov rax, [rsp+560]
    add rax, number
    movq rcx, reg
    cmp rcx, rax
    jne probe_bad
    add rax, 100h
    pshufd xmm0, reg, 4Eh
    movq rcx, xmm0
    cmp rcx, rax
    jne probe_bad
ENDM

; ====== check_gpr_pattern ======

CHECK_GPR MACRO reg, number
    mov rax, [rsp+560]
    add rax, number
    cmp reg, rax
    jne probe_bad
ENDM

; ====== x86_scheduler_probe ======

x86_scheduler_probe PROC
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 600 ; entry8 -64 -600 = aligned16; shadow32 then savedFX512.
    mov [rsp+544], rcx
    mov [rsp+552], rdx
    mov [rsp+560], r8
    fxsave64 [rsp+32]
    mov eax, r8d
    and eax, 3
    shl eax, 13
    or eax, 1F80h
    mov [rsp+568], eax
    ldmxcsr [rsp+568]
    mov eax, r8d
    and eax, 3
    shl eax, 10
    or eax, 37Fh
    mov [rsp+576], ax
    fldcw [rsp+576]
    mov rbx, r8
    add rbx, 1
    mov rbp, r8
    add rbp, 2
    mov rdi, r8
    add rdi, 3
    mov rsi, r8
    add rsi, 4
    mov r12, r8
    add r12, 5
    mov r13, r8
    add r13, 6
    mov r14, r8
    add r14, 7
    mov r15, r8
    add r15, 8
    SET_XMM xmm6, 16
    SET_XMM xmm7, 17
    SET_XMM xmm8, 18
    SET_XMM xmm9, 19
    SET_XMM xmm10, 20
    SET_XMM xmm11, 21
    SET_XMM xmm12, 22
    SET_XMM xmm13, 23
    SET_XMM xmm14, 24
    SET_XMM xmm15, 25
    mov rcx, [rsp+544]
    mov rdx, [rsp+552]
    call x86_scheduler_context_switch
    CHECK_GPR rbx, 1
    CHECK_GPR rbp, 2
    CHECK_GPR rdi, 3
    CHECK_GPR rsi, 4
    CHECK_GPR r12, 5
    CHECK_GPR r13, 6
    CHECK_GPR r14, 7
    CHECK_GPR r15, 8
    CHECK_XMM xmm6, 16
    CHECK_XMM xmm7, 17
    CHECK_XMM xmm8, 18
    CHECK_XMM xmm9, 19
    CHECK_XMM xmm10, 20
    CHECK_XMM xmm11, 21
    CHECK_XMM xmm12, 22
    CHECK_XMM xmm13, 23
    CHECK_XMM xmm14, 24
    CHECK_XMM xmm15, 25
    stmxcsr [rsp+572]
    mov eax, [rsp+572]
    cmp eax, [rsp+568]
    jne probe_bad
    fnstcw [rsp+578]
    mov ax, [rsp+578]
    cmp ax, [rsp+576]
    jne probe_bad
    mov DWORD PTR [rsp+584], 1
    jmp probe_restore
probe_bad:
    mov DWORD PTR [rsp+584], 0
probe_restore:
    fxrstor64 [rsp+32]
    mov eax, [rsp+584]
    add rsp, 600
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ret
x86_scheduler_probe ENDP
END

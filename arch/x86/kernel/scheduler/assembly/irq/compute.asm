option casemap:none
.code
FILL_XMM MACRO index
    pcmpeqd xmm&index, xmm&index
ENDM

; ====== task IRQ workload, Microsoft x64 ABI, no C calls at IF=1 ======

; Intel SDM093 Vol3A §7.8.1: IF9, STI permits external IRQ, CLI closes section.
; Vol2A pp3-148 CLI, 3-354 FLD1, 3-409..419 Table3-48 FXSAVE64, 3-455 IN.
; Port/mask/deadline come from the validated ACPI PM clock; no universal port.
PUBLIC x86_scheduler_irq_compute
x86_scheduler_irq_compute PROC
    sub rsp, 40
    call x86_scheduler_irq_poll
    cmp eax, 2
    jne legacy_result
    xor eax, eax
legacy_result:
    add rsp, 40
    ret
x86_scheduler_irq_compute ENDP

; ====== checked empty polling shares the same context preservation gate ======

PUBLIC x86_scheduler_irq_poll
x86_scheduler_irq_poll PROC
    mov ax, cs
    test al, 3
    jnz rejected
    pushfq
    pop rax
    test eax, 600h
    jnz rejected
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 1608
    mov [rsp+1584], rcx
    mov [rsp+1592], edx
    mov [rsp+1596], r8d
    mov [rsp+1600], r9d
    lea rdi, [rsp+32]
    xor eax, eax
    mov ecx, 192
    rep stosq
    fxsave64 [rsp+32]
    fninit
    fld1
    xmm_index = 0
    REPT 16
        FILL_XMM %xmm_index
        xmm_index = xmm_index + 1
    ENDM
    fxsave64 [rsp+544]
    mov r11, [rsp+1584]
    mov r12d, [rsp+1596]
    mov r14d, [rsp+1600]
    mov edx, [rsp+1592]
    in eax, dx
    and eax, r12d
    mov r13d, eax
    mov r15d, 20000000
    mov ebx, 111h
    mov ebp, 112h
    mov esi, 113h
    mov edi, 114h
    mov r8d, 115h
    mov r9d, 116h
    mov r10d, 117h
    mov DWORD PTR [rsp+1576], 2
    test r14d, r14d
    jnz valid_deadline
    mov DWORD PTR [rsp+1576], 0
    jmp finish_irq ; Zero deadline rejected with IF still clear (isolated timeout test).
valid_deadline:
PUBLIC x86_scheduler_irq_compute_begin
x86_scheduler_irq_compute_begin LABEL BYTE
    sti
irq_work:
    cmp DWORD PTR [r11+4], 0
    jne finish_irq
    cmp DWORD PTR [r11], 1
    je received
    mov edx, [rsp+1592]
    in eax, dx
    sub eax, r13d
    and eax, r12d
    cmp eax, r14d
    jae finish_irq
    dec r15d
    jz finish_irq
    pause
    jmp irq_work
received:
    mov DWORD PTR [rsp+1576], 1
finish_irq:
    cli
PUBLIC x86_scheduler_irq_compute_end
x86_scheduler_irq_compute_end LABEL BYTE
    fxsave64 [rsp+1056]
    cmp ebx, 111h
    jne corrupted
    cmp ebp, 112h
    jne corrupted
    cmp esi, 113h
    jne corrupted
    cmp edi, 114h
    jne corrupted
    cmp r8d, 115h
    jne corrupted
    cmp r9d, 116h
    jne corrupted
    cmp r10d, 117h
    jne corrupted
    lea rsi, [rsp+544]
    lea rdi, [rsp+1056]
    mov ecx, 64
    repe cmpsq
    je restored
corrupted:
    mov DWORD PTR [rsp+1576], 0
restored:
    fxrstor64 [rsp+32]
    mov eax, [rsp+1576]
    add rsp, 1608
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
rejected:
    xor eax, eax
    ret
x86_scheduler_irq_poll ENDP
END

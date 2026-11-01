option casemap:none
EXTERN xp_panic_fault:PROC
EXTERN xp_panic_call:PROC
.code

; ====== save_gpr ======

SAVE_GPR MACRO
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
ENDM

; ====== xp_panic_fault_entry ======

; Intel SDM 078US 6.14.2: normalized vector/error + RIP,CS,RFLAGS,RSP,SS. Capture GPR before the C ABI.
PUBLIC xp_panic_fault_entry
xp_panic_fault_entry PROC
    cli
    SAVE_GPR
    cld
    mov rcx, rsp
    mov rdx, cr2
    mov r8, cr3
    and rsp, -16
    sub rsp, 32
    call xp_panic_fault
    jmp xp_panic_terminal
xp_panic_fault_entry ENDP

; ====== xp_panic_voluntary ======

PUBLIC xp_panic_voluntary
xp_panic_voluntary PROC
    pushfq
    cli
    SAVE_GPR
    cld
    mov rcx, rsp
    mov rdx, cr2
    mov r8, cr3
    xor r9d, r9d
    mov r9w, cs
    shl r9d, 16
    mov r9w, ss
    and rsp, -16
    sub rsp, 32
    call xp_panic_call
    jmp xp_panic_terminal
xp_panic_voluntary ENDP

; ====== xp_panic_apic_id ======

; Intel SDM CPUID leaf 1 EBX[31:24]: initial APIC ID; restricted to the already validated xAPIC profile.
PUBLIC xp_panic_apic_id
xp_panic_apic_id PROC
    push rbx
    mov eax, 1
    xor ecx, ecx
    cpuid
    mov eax, ebx
    shr eax, 24
    pop rbx
    ret
xp_panic_apic_id ENDP

; ====== xp_panic_quiet_stop ======

PUBLIC xp_panic_quiet_stop
xp_panic_quiet_stop PROC
    mov rax, rcx
    jmp xp_panic_terminal
xp_panic_quiet_stop ENDP
xp_panic_terminal LABEL NEAR
    cli
    hlt
    jmp xp_panic_terminal
END

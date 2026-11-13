; Primary sources: Intel SDM revision093, hardware references in docs/design/consolidamento.md and the backend report at 8bad38a.
; Vol.2A CPUID/FXSAVE64/FXRSTOR64/FNINIT/LDMXCSR; Vol.1 ch.21 and §§3.4.3,11.6.6.
; Vol.3A §2.5 CR reads, §13.4.4/Fig13-5 IA32_APIC_BASE 1Bh, BSP bit8.
; Read-only CPU capture: CPL checked BEFORE privileged reads. No CR/MSR writes.
option casemap:none
.const
default_mxcsr DWORD 01F80h
.code

; ====== x86_scheduler_cpu_capture ======

x86_scheduler_cpu_capture PROC
    test rcx, rcx
    jz capture_denied
    mov ax, cs
    and eax, 3
    jnz capture_denied
    push rbx
    mov r8, rcx
    xor eax, eax
    cpuid
    cmp eax, 1
    jb capture_failed
    mov [r8+40], eax
    mov eax, 1
    cpuid
    test edx, 200h ; CPUID.1.EDX.APIC9, prerequisite for RDMSR 1Bh.
    jz capture_failed
    mov [r8+44], edx
    shr ebx, 24 ; CPUID.1.EBX[31:24], initial APIC ID; v0 <=255.
    mov [r8+48], ebx
    mov DWORD PTR [r8+52], 0
    mov QWORD PTR [r8+56], 0
    mov rax, cr0
    mov [r8], rax
    mov rax, cr3
    mov [r8+8], rax
    mov rax, cr4
    mov [r8+16], rax
    pushfq
    pop rax
    mov [r8+24], rax
    mov ecx, 1Bh
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov [r8+32], rax
    mov eax, 1
    pop rbx
    ret
capture_failed:
    pop rbx
capture_denied:
    xor eax, eax
    ret
x86_scheduler_cpu_capture ENDP

; ====== x86_scheduler_fp_template ======

; Caller provides owned writable aligned512 bytes and initialized OSFXSR/EM/TS.
; 512-byte scratch on aligned stack. Restore ALL original FP/SSE state before RET.
; FNINIT default FCW=037Fh; LDMXCSR=1F80h masks exceptions, round-to-nearest.
x86_scheduler_fp_template PROC
    test rcx, rcx
    jz template_denied
    test rcx, 15
    jnz template_denied
    push rbx
    mov r8, rcx
    xor eax, eax
    cpuid
    cmp eax, 1
    jb template_failed
    mov eax, 1
    cpuid
    and edx, 07000000h ; FXSR24/SSE25/SSE2 26, why: instructions used below.
    cmp edx, 07000000h
    jne template_failed
    sub rsp, 512 ; entry8 - push8 -512 = aligned16.
    fxsave64 [rsp]
    fninit
    ldmxcsr default_mxcsr
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    pxor xmm6, xmm6
    pxor xmm7, xmm7
    pxor xmm8, xmm8
    pxor xmm9, xmm9
    pxor xmm10, xmm10
    pxor xmm11, xmm11
    pxor xmm12, xmm12
    pxor xmm13, xmm13
    pxor xmm14, xmm14
    pxor xmm15, xmm15
    fxsave64 [r8]
    fxrstor64 [rsp]
    add rsp, 512
    mov eax, 1
    pop rbx
    ret
template_failed:
    pop rbx
template_denied:
    xor eax, eax
    ret
x86_scheduler_fp_template ENDP
END

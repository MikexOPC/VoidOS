; ====== actual_private_prefix ======

org 0xffffffff80100000
bits 64
default rel
    fninit
    fld1
    fldcw [rel seed_control]
    ldmxcsr [rel seed_mxcsr]
    pcmpeqd xmm0, xmm0
    pcmpeqd xmm1, xmm1
    pcmpeqd xmm2, xmm2
    pcmpeqd xmm3, xmm3
    pcmpeqd xmm4, xmm4
    pcmpeqd xmm5, xmm5
    pcmpeqd xmm6, xmm6
    pcmpeqd xmm7, xmm7
    pcmpeqd xmm8, xmm8
    pcmpeqd xmm9, xmm9
    pcmpeqd xmm10, xmm10
    pcmpeqd xmm11, xmm11
    pcmpeqd xmm12, xmm12
    pcmpeqd xmm13, xmm13
    pcmpeqd xmm14, xmm14
    pcmpeqd xmm15, xmm15
    jmp xs_private_entry
%include "arch/x86/kernel/cpu/percpu/startup/assembly/private/entry.asm"

; ====== assembly_continuation_not_c ======

xs_private_arrive:
    cmp rcx, r15
    jne xs_private_fault
    cmp r8, r15
    jne xs_private_fault
    cmp rdx, [rcx + 16]
    jne xs_private_fault
    mov rax, [gs:0]
    cmp rax, [rcx + 72]
    jne xs_private_fault
    fxsave [r15 - 512]
%if MODE = 1
    mov rax, [rcx + 32]
    mov al, [rax]
%elif MODE = 2
    mov rax, [rcx + 56]
    sub rax, 65536 + 4096
    mov al, [rax]
%endif
    mov dword [r15 + 664], 3
.halt:
    hlt
    jmp .halt

; ====== nondefault_simd_fixture ======

align 4
seed_mxcsr: dd 0x3f80
seed_control: dw 0x27f

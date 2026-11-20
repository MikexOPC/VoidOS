; ====== service_interrupt_contract ======

; Intel SDM 253668-078US 6.14.1/6.14.3-5: 64-bit interrupt gate,
; IST=0 on private kernel stack, SS:RSP saved and popped by IRETQ.
; Intel SDM 253666-089US Vol.2A FXSAVE Table3-48: 512 bytes,
; 16-aligned, includes XMM0-15; FXRSTOR64 restores that format.
; Microsoft x64 ABI: 32-byte shadow space, stack aligned before CALL.
bits 64
default rel
section .text
global x86_irq_begin, x86_irq_end
extern x86_irq_dispatch, x86_irq_after

; ====== vector_entries ======

x86_irq_begin:
%assign v 32
%rep 224
x86_irq_vector_%+v:
    push qword v
    jmp irq_common
%assign v v+1
%endrep

; ====== irq_common ======

irq_common:
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
    cmp qword [r12+120], 0xf0
    jne .profile
    pxor xmm0, xmm0
    pxor xmm15, xmm15
.profile:
%ifdef KERNEL_TEST_AP_SERVICE
    fninit
%assign i 0
%rep 16
    pxor xmm%+i, xmm%+i
%assign i i+1
%endrep
%endif
    cld
    [warning -reloc-rel-dword]
    call x86_irq_dispatch
    [warning +reloc-rel-dword]
    lea rcx, [r12+120]
    [warning -reloc-rel-dword]
    call x86_irq_after
    [warning +reloc-rel-dword]
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
x86_irq_end:

; ====== x86_irq_cpu_state ======

; Intel SDM Vol. 2A CPUID.01H:EDX[26:24] SSE2/SSE/FXSR.
; Vol. 3A 2.5: CR0.EM2/TS3 clear, CR4.OSFXSR9/OSXMMEXCPT10 set, OSXSAVE18 clear.
global x86_irq_cpu_state
x86_irq_cpu_state:
    push rbx
    mov eax, 1
    cpuid
    and edx, 0x7000000
    cmp edx, 0x7000000
    jne .bad
    mov rax, cr0
    test eax, 0xc
    jnz .bad
    mov rax, cr4
    test eax, 0x40000
    jnz .bad
    and eax, 0x600
    cmp eax, 0x600
    jne .bad
    mov eax, 1
    pop rbx
    ret
.bad:
    xor eax, eax
    pop rbx
    ret

; ====== immutable_target_registry ======

section .rdata align=8
global x86_irq_targets
x86_irq_targets:
[warning -reloc-abs-qword]
%assign v 32
%rep 224
    dq x86_irq_vector_%+v
%assign v v+1
%endrep
[warning +reloc-abs-qword]

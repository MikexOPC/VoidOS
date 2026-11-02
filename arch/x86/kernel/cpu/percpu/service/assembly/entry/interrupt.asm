; ====== service_interrupt_contract ======

; Intel SDM 253668-078US 6.14.1/6.14.3-5: 64-bit interrupt gate,
; IST=0 on private kernel stack, SS:RSP saved and popped by IRETQ.
; Intel SDM 253666-089US Vol.2A FXSAVE Table3-48: 512 bytes,
; 16-aligned, includes XMM0-15; FXRSTOR64 restores that format.
; Microsoft x64 ABI: 32-byte shadow space, stack aligned before CALL.
bits 64
default rel
section .text
global xis_irq_begin, xis_irq_end
extern xis_native_dispatch

; ====== vector_entries ======

xis_irq_begin:
%assign v 32
%rep 224
xis_vector_%+v:
    push qword v
    jmp xis_common
%assign v v+1
%endrep

; ====== xis_common ======

xis_common:
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
    fninit
%assign i 0
%rep 16
    pxor xmm%+i, xmm%+i
%assign i i+1
%endrep
    cld
    [warning -reloc-rel-dword]
    call xis_native_dispatch
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
xis_irq_end:

; ====== immutable_target_registry ======

section .rdata align=8
global xis_irq_targets
xis_irq_targets:
[warning -reloc-abs-qword]
%assign v 32
%rep 224
    dq xis_vector_%+v
%assign v v+1
%endrep
[warning +reloc-abs-qword]

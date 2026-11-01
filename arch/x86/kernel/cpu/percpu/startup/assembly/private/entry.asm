; ====== private_entry_contract ======

; Intel SDM 253668-078US 2.4/3.4.5/6.14.1: GDTR/IDTR, code L=1/D=0,
; 16-byte interrupt gates type 0x8e, ring zero, initially IST=0.
; 6.7.1: no IRET here; NMI remains blocked until the admitted panic path exists.
; 8.2.4/8.7: LTR selector 0x18 loads the private 104-byte TSS, setting busy.
; Vol.4 335592-078US Table 2-2: IA32_GS_BASE=0xc0000101;
; Vol.3A 3.4.4: canonical per-CPU base, no SWAPGS.
; 2.5/4.5: CR3 is a prevalidated 4-level physical root, PCIDE/LA57 disabled.
; 10.2.1/14.1.3: FNINIT and MXCSR=0x1f80 initialize the x87/SSE profile.
; Software ABI: 1024-byte private frame; record 0, IDT 128, IDTR 640,
; MXCSR 656, phase/error 664/668, bridge GDT 688, bridge GDTR 720.
bits 64
default rel
%ifidn __OUTPUT_FORMAT__,win64
section .text
global xs_private_entry
global xs_private_fault
extern xs_private_arrive
%endif

; ====== xs_private_entry ======

xs_private_entry:
    cli
    cld
    cmp dword [rcx], 1
    jne xs_private_early_stop
    cmp dword [rcx + 4], 104
    jne xs_private_early_stop
    cmp [rcx + 16], rdx
    jne xs_private_early_stop
    mov rsi, rcx
    mov rsp, [rsi + 56]
    sub rsp, 1024
    mov r15, rsp
    mov rdi, rsp
    mov ecx, 128
    xor eax, eax
    rep stosq
    mov rdi, r15
    mov ecx, 13
    rep movsq
    mov dword [r15 + 664], 1

; ====== high_emergency_tables ======

    lea rax, [rel xs_private_fault]
    lea rdi, [r15 + 128]
    mov ecx, 32
.gate:
    mov [rdi], ax
    mov word [rdi + 2], 0x18
    mov byte [rdi + 5], 0x8e
    mov rdx, rax
    shr rdx, 16
    mov [rdi + 6], dx
    shr rdx, 16
    mov [rdi + 8], edx
    add rdi, 16
    loop .gate
    mov word [r15 + 640], 511
    lea rax, [r15 + 128]
    mov [r15 + 642], rax
    mov rax, 0x00af9b000000ffff
    mov [r15 + 696], rax
    mov [r15 + 712], rax
    mov rax, 0x00cf93000000ffff
    mov [r15 + 704], rax
    mov word [r15 + 720], 31
    lea rax, [r15 + 688]
    mov [r15 + 722], rax
    lidt [r15 + 640]
    lgdt [r15 + 720]
    push qword 8
    lea rax, [rel xs_private_cs]
    push rax
    retfq

; ====== xs_private_cs ======

xs_private_cs:
    lea rdi, [r15 + 128]
    mov ecx, 32
.gate:
    mov word [rdi + 2], 8
    add rdi, 16
    loop .gate
    mov rax, [r15 + 48]
    mov cr3, rax
    mov rdx, cr3
    cmp rax, rdx
    jne xs_private_fault
    mov rbx, [r15 + 64]
    lgdt [rbx + 152]
    mov ax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov ax, 0x18
    ltr ax
    mov rax, [r15 + 72]
    mov rdx, rax
    shr rdx, 32
    mov ecx, 0xc0000101
    wrmsr
    fninit
    mov dword [r15 + 656], 0x1f80
    ldmxcsr [r15 + 656]
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
    mov dword [r15 + 664], 2
    xor esi, esi
    xor edi, edi
    mov rcx, r15
    mov rdx, [r15 + 16]
    mov r8, r15
    ; Same-image REL32 is resolved and range-checked by LINK, not a flat offset.
    [warning -reloc-rel-dword]
    jmp xs_private_arrive
    [warning +reloc-rel-dword]

; ====== xs_private_fault ======

xs_private_fault:
    cli
    mov dword [r15 + 668], 1
    mov dword [r15 + 664], -1
.halt:
    hlt
    jmp .halt

; ====== xs_private_early_stop ======

xs_private_early_stop:
    cli
.halt:
    hlt
    jmp .halt

; ====== checked_private_return ======

; Intel SDM 253668-078US 6.7.1: even a faulting IRET unblocks NMI.
; 6.14.1/6.14.3-5: 16-byte gates, private IST1/2/3, IRETQ pops SS:RSP
; even at CPL0. Fixed SS=10h, CS=8, RFLAGS=2 keeps IF/DF/IOPL/NT clear.
; 2.4/8.2.4/8.7: SGDT/STR and loaded private TSS, IST offsets 36/44/52.
; 3.4.4, Vol.4 335592-078US Table 2-2: IA32_GS_BASE C0000101h.
; Frame+736 is a trusted, numerically validated software plan, not user input.
; Per-CPU software ABI: Self=0, EarlyContext=8, Index=16, State=20; ACTIVE=2.
bits 64
default rel
%ifidn __OUTPUT_FORMAT__,win64
section .text
global xs_private_resume
extern xs_private_fault
%endif

; ====== xs_private_resume ======

xs_private_resume:
    cli
    cld
    cmp rcx, r15
    jne xsr_reject
    lea rbx, [r15 + 736]
    cmp dword [rbx], 1
    jne xsr_reject
    cmp dword [rbx + 4], 64
    jne xsr_reject
    cmp qword [rbx + 56], 0
    jne xsr_reject
    mov rax, [rbx + 8]
    cmp rax, [r15 + 16]
    jne xsr_reject
    cmp [rbx + 16], r15
    jne xsr_reject
    mov rax, [rbx + 48]
    cmp rax, [r15 + 48]
    jne xsr_reject
    mov rdx, cr3
    cmp rax, rdx
    jne xsr_reject
    mov rax, [rbx + 40]
    cmp rax, [r15 + 72]
    jne xsr_reject
    mov r9, rax
    mov ecx, 0xc0000101
    rdmsr
    shl rdx, 32
    or rax, rdx
    cmp rax, r9
    jne xsr_reject
    cmp [gs:0], r9
    jne xsr_reject
    cmp qword [gs:8], 0
    jne xsr_reject
    mov eax, [r15 + 8]
    test eax, eax
    jz xsr_reject
    cmp [gs:16], eax
    jne xsr_reject
    cmp dword [gs:20], 2
    jne xsr_reject

; ====== loaded_private_tss ======

    str ax
    cmp ax, 0x18
    jne xsr_reject
    sgdt [r15 + 720]
    cmp word [r15 + 720], 55
    jne xsr_reject
    mov rsi, [r15 + 64]
    cmp [r15 + 722], rsi
    jne xsr_reject
    cmp byte [rsi + 29], 0x8b
    jne xsr_reject
    mov rax, [r15 + 56]
    add rax, 24576
    cmp [rsi + 100], rax
    jne xsr_reject
    add rax, 24576
    cmp [rsi + 108], rax
    jne xsr_reject
    add rax, 24576
    cmp [rsi + 116], rax
    jne xsr_reject
    add rax, 24576
    cmp [rsi + 68], rax
    jne xsr_reject

; ====== final_idt_before_unblocking ======

    mov r10, 0xffff800000000000
    mov r11, [rbx + 24]
    cmp r11, r10
    jb xsr_reject
    mov rsi, [rbx + 32]
    cmp rsi, r10
    jb xsr_reject
    cmp word [rsi], 511
    jne xsr_reject
    mov rdi, [rsi + 2]
    cmp rdi, r10
    jb xsr_reject
    test rdi, 15
    jnz xsr_reject
    mov rax, rdi
    add rax, 511
    jc xsr_reject
    xor ecx, ecx
.gate:
    cmp word [rdi + 2], 8
    jne xsr_reject
    cmp byte [rdi + 5], 0x8e
    jne xsr_reject
    cmp dword [rdi + 12], 0
    jne xsr_reject
    mov al, 1
    cmp ecx, 2
    jne .double
    mov al, 2
.double:
    cmp ecx, 8
    jne .ist
    mov al, 3
.ist:
    cmp [rdi + 4], al
    jne xsr_reject
    mov eax, [rdi + 8]
    shl rax, 32
    movzx edx, word [rdi + 6]
    shl rdx, 16
    or rax, rdx
    movzx edx, word [rdi]
    or rax, rdx
    cmp rax, r10
    jb xsr_reject
    add rdi, 16
    inc ecx
    cmp ecx, 32
    jb .gate

; ====== iretq_to_private_continuation ======

    lidt [rsi]
    mov rsp, r15
    push qword 0x10
    push r15
    push qword 2
    push qword 8
    push r11
    mov rcx, r15
    mov rdx, [rbx + 8]
    mov r8, r15
xs_return_iret:
    iretq

; ====== xsr_reject ======

xsr_reject:
    [warning -reloc-rel-dword]
    jmp xs_private_fault
    [warning +reloc-rel-dword]

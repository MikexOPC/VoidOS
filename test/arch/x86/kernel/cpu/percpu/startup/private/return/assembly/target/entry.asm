; ====== private_iret_destination_fixture ======

org 0xffffffff80102000
bits 64
default rel
target_entry:
    cmp rcx, r15
    jne .bad
    cmp r8, r15
    jne .bad
    cmp rdx, [rcx + 16]
    jne .bad
    cmp rsp, r15
    jne .bad
    mov rax, cr3
    cmp rax, [rcx + 48]
    jne .bad
    pushfq
    pop rax
    test rax, (1 << 9) | (1 << 10) | (3 << 12) | (1 << 14)
    jnz .bad
    mov dword [r15 + 664], 4
.halt:
    hlt
    jmp .halt
.bad:
    mov dword [r15 + 668], 3
    mov dword [r15 + 664], -1
    jmp .halt

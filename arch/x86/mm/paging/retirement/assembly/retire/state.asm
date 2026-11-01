option casemap:none
.code

; ====== x86_retire_capture ======

; Intel SDM 090US 2.4.1/2/3/4: SGDT, SIDT, STR, SLDT observe architectural table state.
; Caller-provided 120-byte snapshot; [rsp+8..+17] is Microsoft x64 caller shadow space.
PUBLIC x86_retire_capture
x86_retire_capture PROC
    pushfq
    pop rax
    mov [rcx+32], rax
    lea rax, capture_rip
capture_rip:
    mov [rcx+40], rax
    mov [rcx+48], rsp
    sgdt FWORD PTR [rsp+8]
    mov rax, QWORD PTR [rsp+10]
    mov [rcx+56], rax
    movzx eax, WORD PTR [rsp+8]
    mov [rcx+64], rax
    sidt FWORD PTR [rsp+8]
    mov rax, QWORD PTR [rsp+10]
    mov [rcx+72], rax
    movzx eax, WORD PTR [rsp+8]
    mov [rcx+80], rax
    xor eax, eax
    str ax
    mov [rcx+88], rax
    mov ax, cs
    mov [rcx+96], rax
    mov ax, ss
    mov [rcx+104], rax
    sldt ax
    mov [rcx+112], rax
    ret
x86_retire_capture ENDP

; ====== x86_retire_commit ======

; Intel SDM 090US 5.10.4.1/2: PML4[0].P0 removal followed by MOV CR3, with PGE7/PCIDE17=0.
; RCX is the owned recursive root view, RDX the unchanged root, R8 the old entry, R9 CR4.
PUBLIC x86_retire_commit
x86_retire_commit PROC
    pushfq
    pop rax
    test eax, 200h
    jnz commit_failed
    mov rax, cr3
    cmp rax, rdx
    jne commit_failed
    mov rax, cr4
    cmp rax, r9
    jne commit_failed
    test eax, 21080h
    jnz commit_failed
    cmp QWORD PTR [rcx], r8
    jne commit_failed
    mov QWORD PTR [rcx], 0
    mov cr3, rdx
    mov eax, 1
    ret
commit_failed:
    xor eax, eax
    ret
x86_retire_commit ENDP
END

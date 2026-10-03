option casemap:none
.code

; ====== letture CPU privilegiate soltanto dopo feature checks e IF zero ===== ;

PUBLIC x86_apic_read_profile
x86_apic_read_profile PROC
    test rcx, rcx
    jz capture_null
    push rbx
    push rdi
    mov rdi, rcx
    pushfq
    pop rax
    test eax, 200h
    jnz capture_fail
    mov QWORD PTR [rdi+16], rax
    xor eax, eax
    xor ecx, ecx
    cpuid
    cmp eax, 1
    jb capture_fail
    mov DWORD PTR [rdi+36], eax
    mov eax, 1
    xor ecx, ecx
    cpuid
    mov DWORD PTR [rdi+24], ebx
    mov DWORD PTR [rdi+28], ecx
    mov DWORD PTR [rdi+32], edx
    and edx, 10220h
    cmp edx, 10220h
    jne capture_fail
    mov eax, 80000000h
    xor ecx, ecx
    cpuid
    cmp eax, 80000008h
    jb capture_fail
    mov eax, 80000008h
    xor ecx, ecx
    cpuid
    movzx eax, al
    cmp eax, 32
    jb capture_fail
    cmp eax, 52
    ja capture_fail
    mov DWORD PTR [rdi+40], eax
    mov DWORD PTR [rdi+44], 0
    mov ecx, 1bh
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov QWORD PTR [rdi], rax
    mov ecx, 277h
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov QWORD PTR [rdi+8], rax
    mov eax, 1
    pop rdi
    pop rbx
    ret
capture_fail:
    xor eax, eax
    pop rdi
    pop rbx
    ret
capture_null:
    xor eax, eax
    ret
x86_apic_read_profile ENDP
END

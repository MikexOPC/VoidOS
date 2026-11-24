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
; ====== x86_apic_enable_pat_wc ======
; Intel SDM Vol. 3A, Section 12.12.2 Table 12-10: IA32_PAT (MSR 0x277), PAT4 = bits 39:32.
; Program PAT4 from default WB (0x06) to WC (0x01) after CPUID.01H:EDX[5|16] and IF=0 check.

PUBLIC x86_apic_enable_pat_wc
x86_apic_enable_pat_wc PROC
    push rbx
    pushfq
    pop rax
    test eax, 200h
    jnz pat_wc_fail
    xor eax, eax
    xor ecx, ecx
    cpuid
    cmp eax, 1
    jb pat_wc_fail
    mov eax, 1
    xor ecx, ecx
    cpuid
    and edx, 10020h
    cmp edx, 10020h
    jne pat_wc_fail
    mov ecx, 277h
    rdmsr
    cmp dl, 06h
    jne pat_wc_done
    and edx, 0ffffff00h
    or edx, 01h
    mov r8d, eax
    mov r9d, edx
    mov ecx, 277h
    wrmsr
    mov ecx, 277h
    rdmsr
    cmp eax, r8d
    jne pat_wc_fail
    cmp edx, r9d
    jne pat_wc_fail
pat_wc_done:
    mov eax, 1
    pop rbx
    ret
pat_wc_fail:
    xor eax, eax
    pop rbx
    ret
x86_apic_enable_pat_wc ENDP

; ====== IF zero e caching normale senza cambiare CR0 ===== ;

PUBLIC x86_apic_cache_state
x86_apic_cache_state PROC
    pushfq
    pop rax
    test eax, 200h
    jnz cache_bad
    mov rax, cr0
    test eax, 60000000h
    jnz cache_bad
    mov eax, 1
    ret
cache_bad:
    xor eax, eax
    ret
x86_apic_cache_state ENDP
END

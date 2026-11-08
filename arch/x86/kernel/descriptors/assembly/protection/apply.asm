option casemap:none

.code

; ====== x86_user_cpu_features ======

; Intel SDM 3A §2.5: CPUID.07H.00H:EBX riporta SMEP e SMAP. CPUID altera RBX, non volatile.
PUBLIC x86_user_cpu_features
x86_user_cpu_features PROC
    push rbx
    mov r8, rcx
    mov r9, rdx
    xor eax, eax
    xor ecx, ecx
    cpuid
    mov DWORD PTR [r8], eax
    xor ebx, ebx
    cmp eax, 7
    jb features_store
    mov eax, 7
    xor ecx, ecx
    cpuid
features_store:
    mov DWORD PTR [r9], ebx
    pop rbx
    ret
x86_user_cpu_features ENDP

; ====== x86_user_protection_apply ======

; Intel SDM 3A §2.5: CR4.SMEP bit 20, CR4.SMAP bit 21. Rilettura prima di dichiarare l'esito.
PUBLIC x86_user_protection_apply
x86_user_protection_apply PROC
    mov rax, cr4
    or rax, rcx
    mov cr4, rax
    mov rax, cr4
    and rax, rcx
    cmp rax, rcx
    sete al
    movzx eax, al
    ret
x86_user_protection_apply ENDP

END

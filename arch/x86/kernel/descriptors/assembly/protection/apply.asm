option casemap:none

.code

; ====== x86_user_cpu_features ======

; Intel SDM 2A, CPUID: CPUID.07H.00H:EBX riporta SMEP e SMAP, ECX riporta UMIP.
; Microsoft x64 ABI: RCX, RDX, R8 = puntatori a max leaf, EBX ed ECX del leaf 7. CPUID altera RBX, non volatile.
PUBLIC x86_user_cpu_features
x86_user_cpu_features PROC
    push rbx
    mov r9, rcx
    mov r10, rdx
    mov r11, r8
    xor eax, eax
    xor ecx, ecx
    cpuid
    mov DWORD PTR [r9], eax
    xor ebx, ebx
    xor ecx, ecx
    cmp eax, 7
    jb features_store
    mov eax, 7
    xor ecx, ecx
    cpuid
features_store:
    mov DWORD PTR [r10], ebx
    mov DWORD PTR [r11], ecx
    pop rbx
    ret
x86_user_cpu_features ENDP

; ====== x86_user_protection_apply ======

; Intel SDM 3A §2.5: CR4.UMIP bit 11, CR4.SMEP bit 20, CR4.SMAP bit 21. Rilettura prima di dichiarare l'esito.
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

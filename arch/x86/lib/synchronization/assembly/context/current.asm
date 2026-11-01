option casemap:none
.code

; ====== ks_boot_cpu ======

; Intel SDM 090US Vol. 2A CPUID: leaf 1 EDX.MSR5/APIC9; maximum basic leaf first.
; Vol. 3A 13.4.2-13.4.4, Fig.13-5: MSR 1Bh, BSP bit8. Read only; never enable an AP here.
; RBX is preserved under Microsoft x64 ABI. An AP or unsupported CPU is rejected before C state is read.
PUBLIC ks_boot_cpu
ks_boot_cpu PROC
    push rbx
    xor eax, eax
    cpuid
    cmp eax, 1
    jb current_unsupported
    mov eax, 1
    xor ecx, ecx
    cpuid
    and edx, 220h
    cmp edx, 220h
    jne current_unsupported
    mov ecx, 1bh
    rdmsr
    shr eax, 8
    and eax, 1
    pop rbx
    ret
current_unsupported:
    xor eax, eax
    pop rbx
    ret
ks_boot_cpu ENDP
END

option casemap:none
.code

; ====== xp_gs_supported ======

; Intel SDM 093US Vol.2A CPUID: max leaves first, 1.EDX[5] MSR, 80000001H.EDX[29] long mode.
; Vol.4 Table 2-2: IA32_GS_BASE exists with long mode. Preserve RBX for Microsoft x64 ABI.
PUBLIC xp_gs_supported
xp_gs_supported PROC
    push rbx
    xor eax, eax
    cpuid
    cmp eax, 1
    jb unsupported
    mov eax, 1
    xor ecx, ecx
    cpuid
    bt edx, 5
    jnc unsupported
    mov eax, 80000000h
    cpuid
    cmp eax, 80000001h
    jb unsupported
    mov eax, 80000001h
    xor ecx, ecx
    cpuid
    bt edx, 29
    jnc unsupported
    mov eax, 1
    pop rbx
    ret
unsupported:
    xor eax, eax
    pop rbx
    ret
xp_gs_supported ENDP

; ====== xp_gs_read ======

; Intel SDM 093US Vol.4 Table 2-2: C0000101H IA32_GS_BASE, all 64 address bits, R/W.
; Vol.3A 3.4.4: CPL0 MSR access; reader validates this base before any GS memory operand.
PUBLIC xp_gs_read
xp_gs_read PROC
    mov ecx, 0c0000101h
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret
xp_gs_read ENDP

; ====== xp_gs_write ======

; Intel SDM 093US Vol.3A 3.4.4, Vol.4 Table 2-2: canonical 64-bit IA32_GS_BASE, C0000101H.
; C verifies known high kernel storage and quiescence first; no FSGSBASE/CR4 modification.
PUBLIC xp_gs_write
xp_gs_write PROC
    mov rax, rcx
    mov rdx, rcx
    shr rdx, 32
    mov ecx, 0c0000101h
    wrmsr
    ret
xp_gs_write ENDP

; ====== xp_gs_self ======

; Intel SDM 093US Vol.3A 3.4.4: effective address uses GS.base plus displacement.
; Software ABI Self is offset 0 (percpu.h static_assert); base is checked before this read.
PUBLIC xp_gs_self
xp_gs_self PROC
    mov rax, QWORD PTR gs:[0]
    ret
xp_gs_self ENDP
END

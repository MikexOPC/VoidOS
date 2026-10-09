; Intel SDM093 Vol3A §2.2.1/Table2-1: IA32_EFER C0000080h, LMA10/NXE11.
; Vol1 ch21: CPUID80000001.EDX LM29/NX20 after max extended leaf check.
; Read-only gate; CPL before privileged RDMSR, no control register changes.
option casemap:none
.code

; ====== x86_scheduler_cpu_permissions ======

x86_scheduler_cpu_permissions PROC
    mov ax, cs
    and eax, 3
    jnz denied
    push rbx
    mov eax, 80000000h
    cpuid
    cmp eax, 80000001h
    jb failed
    mov eax, 80000001h
    cpuid
    and edx, 20100000h
    cmp edx, 20100000h
    jne failed
    mov ecx, 0C0000080h
    rdmsr
    and eax, 0C00h
    cmp eax, 0C00h
    jne failed
    mov eax, 1
    pop rbx
    ret
failed:
    pop rbx
denied:
    xor eax, eax
    ret
x86_scheduler_cpu_permissions ENDP
END

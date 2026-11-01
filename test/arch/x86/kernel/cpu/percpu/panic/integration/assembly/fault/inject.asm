option casemap:none
EXTERN kernel_panic:PROC
.code

; ====== xp_panic_test_fault ======

; Intel SDM 078US 6.15: #UD=6, #GP=13, #PF=14; these instructions belong only to the isolated fault image.
PUBLIC xp_panic_test_fault
xp_panic_test_fault PROC
    cmp ecx, 2
    je page_fault
    cmp ecx, 3
    je selector_fault
    cmp ecx, 4
    je simd_guard
    cmp ecx, 5
    je simd_call
    ud2
page_fault:
    xor eax, eax
    mov al, BYTE PTR [rax]
    ud2
selector_fault:
    mov ax, 40h
    mov ds, ax
    ud2
simd_guard:
    ; Intel SDM 078US 2.5 CR0.TS bit 3: any accidental SSE/FP instruction would raise #NM during the panic.
    mov rax, cr0
    or rax, 8
    mov cr0, rax
    ud2
simd_call:
    mov rax, cr0
    or rax, 8
    mov cr0, rax
    mov rcx, -1
    sub rsp, 40
    call kernel_panic
    ud2
xp_panic_test_fault ENDP
END

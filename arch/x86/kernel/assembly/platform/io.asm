option casemap:none
; H1-A/H2: paging generation, defined in arch/x86/mm/paging/inventory.c.
EXTERN x86_paging_generation_counter:QWORD
.code

; ====== accessi byte a porte I O del profilo diagnostico ====== ;

PUBLIC x86_read_port
x86_read_port PROC
    mov dx, cx
    in al, dx
    ret
x86_read_port ENDP
PUBLIC x86_write_port
x86_write_port PROC
    mov al, dl
    mov dx, cx
    out dx, al
    ret
x86_write_port ENDP

; ====== root realmente attiva e arresto autonomo senza firmware ====== ;

PUBLIC x86_kernel_root
x86_kernel_root PROC
    mov rax, cr3
    ret
x86_kernel_root ENDP
PUBLIC x86_kernel_stop
x86_kernel_stop PROC
    cli
    mov rax, rcx
halt_loop:
    hlt
    jmp halt_loop
x86_kernel_stop ENDP

; ====== x86_kernel_idle ======

; Intel SDM Vol. 2B, STI: IF = 1 takes effect after the next instruction, so STI; HLT cannot
; lose a wakeup. Intel SDM Vol. 2A, HLT: an interrupt or NMI resumes execution after HLT.
PUBLIC x86_kernel_idle
x86_kernel_idle PROC
idle_loop:
    sti
    hlt
    jmp idle_loop
x86_kernel_idle ENDP
; ====== limite fisico CPUID preservando RBX secondo ABI Microsoft x64 ====== ;

PUBLIC x86_physical_bits
x86_physical_bits PROC
    push rbx
    mov eax, 80000000h
    xor ecx, ecx
    cpuid
    cmp eax, 80000008h
    jb unsupported_bits
    mov eax, 80000008h
    xor ecx, ecx
    cpuid
    movzx eax, al
    pop rbx
    ret
unsupported_bits:
    xor eax, eax
    pop rbx
    ret
x86_physical_bits ENDP
; ====== invalidazione locale dopo modifica della PTE senza PCID o SMP ====== ;

PUBLIC x86_invalidate_page
x86_invalidate_page PROC
    invlpg BYTE PTR [rcx]
    ; Every PTE change ends here: one more paging generation (single CPU, no lock needed).
    inc QWORD PTR [x86_paging_generation_counter]
    ret
x86_invalidate_page ENDP
END

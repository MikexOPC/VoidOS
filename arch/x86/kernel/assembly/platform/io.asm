option casemap:none
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
    ret
x86_invalidate_page ENDP
END

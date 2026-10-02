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
END

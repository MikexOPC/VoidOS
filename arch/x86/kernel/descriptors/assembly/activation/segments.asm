option casemap:none
.code

; ====== caricamento GDT e TSS dopo commit dello storage persistente ===== ;

PUBLIC x86_kernel_segments_activate
x86_kernel_segments_activate PROC
    lgdt FWORD PTR [rcx]
    mov ax, 24
    ltr ax
    mov ax, 16
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    push 8
    lea rax, reload_code
    push rax
    db 48h, 0cbh ; REX.W + RETF: due slot da 8 byte, RIP seguito da CS.
reload_code:
    ret
x86_kernel_segments_activate ENDP

; ====== GDTR reale dopo la transizione senza salvare indirizzi presunti ===== ;

PUBLIC x86_kernel_segments_read
x86_kernel_segments_read PROC
    sgdt FWORD PTR [rcx]
    ret
x86_kernel_segments_read ENDP
END

option casemap:none
.code

; ====== attivazione solo nell'immagine sacrificabile del test QEMU ====== ;

PUBLIC boot_test_trigger_exception
boot_test_trigger_exception PROC
    cli
    lgdt FWORD PTR [rcx]
    lidt FWORD PTR [rdx]
    cmp r8d, 6
    je invalid_opcode
    cmp r8d, 13
    je invalid_selector
    xor eax, eax
    xor edx, edx
    div eax
invalid_opcode:
    ud2
invalid_selector:
    mov ax, 18h
    mov ds, ax
    ud2
boot_test_trigger_exception ENDP
END

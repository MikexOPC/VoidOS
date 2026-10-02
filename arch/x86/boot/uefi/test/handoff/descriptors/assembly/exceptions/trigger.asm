option casemap:none

; ====== serializzazione della copia preservando gli argomenti del probe ====== ;

SERIALIZE_COPY MACRO
    push rbx
    push rcx
    push rdx
    xor eax, eax
    cpuid
    pop rdx
    pop rcx
    pop rbx
ENDM
.code

; ====== attivazione solo nell'immagine sacrificabile del test QEMU ====== ;

PUBLIC boot_test_trigger_exception
boot_test_trigger_exception PROC
    SERIALIZE_COPY
    cli
    lgdt FWORD PTR [rcx]
    lidt FWORD PTR [rdx]
    mov ax, 18h
    ltr ax
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
    mov ax, 28h
    mov ds, ax
    ud2
boot_test_trigger_exception ENDP

; ====== prova del tratto finale con stack e contratto dedicati ====== ;

PUBLIC boot_test_trigger_capsule
boot_test_trigger_capsule PROC
    SERIALIZE_COPY
    mov r11, r8
    mov r10, QWORD PTR [rsp + 40]
    mov r8, QWORD PTR [rsp + 48]
    cli
    lgdt FWORD PTR [rcx]
    lidt FWORD PTR [rdx]
    mov ax, 18h
    ltr ax
    mov rcx, r10
    mov rdx, r9
    std
    jmp r11
boot_test_trigger_capsule ENDP

PUBLIC boot_test_capsule_target
PUBLIC boot_test_capsule_stop
boot_test_capsule_target PROC
    mov rax, rsp
    mov rdx, rcx
boot_test_capsule_stop::
    cli
capsule_wait:
    hlt
    jmp capsule_wait
boot_test_capsule_target ENDP
END

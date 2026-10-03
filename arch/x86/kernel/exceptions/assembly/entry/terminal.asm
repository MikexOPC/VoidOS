option casemap:none
EXTERN x86_exception_terminal:PROC
.code

; ====== lettura IDTR e caricamento sullo stack gia validato dal loader ===== ;

PUBLIC x86_exception_read_idt
x86_exception_read_idt PROC
    sidt FWORD PTR [rcx]
    ret
x86_exception_read_idt ENDP

; ====== attivazione della tabella gia verificata in C ===== ;

PUBLIC x86_exception_load_idt
x86_exception_load_idt PROC
    lidt FWORD PTR [rcx]
    ret
x86_exception_load_idt ENDP

; ====== selettori ereditati e interrupt disabilitati prima del cambio IDT ===== ;

PUBLIC x86_exception_environment
x86_exception_environment PROC
    xor eax, eax
    str ax
    cmp ax, 24
    jne invalid_environment
    mov ax, cs
    cmp ax, 8
    jne invalid_environment
    mov ax, ss
    cmp ax, 16
    jne invalid_environment
    pushfq
    pop rax
    test eax, 200h
    jnz invalid_environment
    mov eax, 1
    ret
invalid_environment:
    xor eax, eax
    ret
x86_exception_environment ENDP

; ====== frame uniforme vettore error code RIP CS RFLAGS RSP SS ===== ;

FAULT_ENTRY MACRO vector, cpu_error
x86_kernel_fault_&vector LABEL BYTE
    IF cpu_error EQ 0
        push 0
    ENDIF
    push vector
    jmp NEAR PTR x86_kernel_fault_common
ENDM
PUBLIC x86_kernel_fault_begin
x86_kernel_fault_begin LABEL BYTE
IRP vector, <0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31>
    FAULT_ENTRY vector, 0
ENDM
IRP vector, <8,10,11,12,13,14,17,21,29,30>
    FAULT_ENTRY vector, 1
ENDM

; ====== terminale senza ritorno shadow space e allineamento Microsoft x64 ===== ;

x86_kernel_fault_common LABEL BYTE
    mov al, 1
    xchg al, BYTE PTR [x86_kernel_fault_entered]
    test al, al
    jnz NEAR PTR x86_kernel_nested_stop
    cld
    mov rcx, rsp
    mov rdx, cr2
    and rsp, -16
    sub rsp, 32
    call x86_exception_terminal
    cli
terminal_fallback:
    hlt
    jmp terminal_fallback

; ====== secondo ingresso arrestato senza richiamare C o la seriale ===== ;

x86_kernel_nested_stop LABEL BYTE
    cli
    mov rax, 4b4e455354450001h
nested_halt:
    hlt
    jmp nested_halt
PUBLIC x86_kernel_fault_end
x86_kernel_fault_end LABEL BYTE

; ====== indirizzi dei trentadue ingressi in memoria non eseguibile ===== ;

.const
ALIGN 8
PUBLIC x86_kernel_fault_targets
x86_kernel_fault_targets LABEL QWORD
IRP vector, <0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31>
    DQ x86_kernel_fault_&vector
ENDM
.data
x86_kernel_fault_entered DB 0
END

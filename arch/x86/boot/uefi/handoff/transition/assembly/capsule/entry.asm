option casemap:none
.code

; ====== tratto finale isolato dopo preflight del probe ABI ====== ;

PUBLIC x86_boot_capsule_begin
x86_boot_capsule_begin LABEL BYTE
    cli
    cld
    mov rax, rcx
    mov rsp, rdx
    sub rsp, 40
    mov QWORD PTR [rsp], 0
    mov rcx, r8
    xor ebp, ebp
    jmp rax
    DB (64 - ($ - x86_boot_capsule_begin)) DUP (0cch)

; ====== salti relativi interni indipendenti dalla base di caricamento ====== ;

CAPSULE_VECTOR MACRO vector
    mov eax, vector
    DB 0e9h
    DD x86_boot_capsule_stop - ($ + 4)
    DB 6 DUP (0cch)
ENDM
IRP vector, <0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31>
    CAPSULE_VECTOR vector
ENDM

; ====== arresto terminale senza memoria esterna o ritorno ====== ;

x86_boot_capsule_stop LABEL BYTE
    cli
capsule_halt:
    hlt
    jmp SHORT capsule_halt
PUBLIC x86_boot_capsule_end
    DB (640 - ($ - x86_boot_capsule_begin)) DUP (0cch)

; ====== ambiente autonomo prima di CR3 senza servizi firmware ====== ;

capsule_activate:
    cli
    cld
    mov r12, rcx
    mov rax, cr4
    test eax, 01821000h
    jnz activation_failed
    mov ecx, 0c0000080h
    rdmsr
    or eax, 800h
    wrmsr
    mov rax, cr0
    or rax, 10000h
    mov cr0, rax
    mov rax, QWORD PTR [r12 + 40]
    lgdt FWORD PTR [rax]
    mov ax, 18h
    ltr ax
    mov rax, QWORD PTR [r12 + 48]
    lidt FWORD PTR [rax]
    push 8
    DB 48h, 8dh, 05h
    DD capsule_cs_ready - ($ + 4)
    push rax
    DB 48h, 0cbh
capsule_cs_ready:
    mov ax, 10h
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rax, cr0
    test eax, 10000h
    jz activation_failed
    mov ecx, 0c0000080h
    rdmsr
    test eax, 800h
    jz activation_failed
    mov rax, QWORD PTR [r12 + 24]
    mov QWORD PTR [rax + 24], 3
    mov rax, cr4
    and rax, NOT 80h
    mov cr4, rax
    mov rax, QWORD PTR [r12]
    mov cr3, rax
    mov rsp, QWORD PTR [r12 + 16]
    mov rax, cr3
    cmp rax, QWORD PTR [r12]
    jne activation_failed
    sub rsp, 32
    mov rcx, QWORD PTR [r12 + 32]
    call QWORD PTR [r12 + 8]
    jmp NEAR PTR capsule_success_stop
activation_failed:
    mov eax, 0b007ffffh
capsule_success_stop:
    cli
activation_halt:
    hlt
    jmp activation_halt
x86_boot_capsule_end LABEL BYTE

; ====== adattatori eseguiti prima di entrare nel codice posseduto ====== ;

PUBLIC x86_enter_transition
x86_enter_transition PROC
    push rbx
    push rcx
    push rdx
    xor eax, eax
    cpuid
    pop rdx
    pop rcx
    pop rbx
    jmp rdx
x86_enter_transition ENDP
PUBLIC x86_stop_transition
x86_stop_transition PROC
    cli
    mov eax, 0b007eeeeh
restricted_halt:
    hlt
    jmp restricted_halt
x86_stop_transition ENDP
END

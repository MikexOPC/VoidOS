option casemap:none
.code

; ====== tratto finale chiamabile solo dopo il futuro preflight hardware ====== ;

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
x86_boot_capsule_end LABEL BYTE
END

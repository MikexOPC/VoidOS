option casemap:none

; ====== ingressi senza uso aggiuntivo dello stack o ritorno ====== ;

TERMINAL_ENTRY MACRO vector
x86_exception_&vector LABEL BYTE
    mov eax, vector
    jmp NEAR PTR x86_boot_exception_stop
ENDM

.code
TERMINAL_ENTRY 0
TERMINAL_ENTRY 1
TERMINAL_ENTRY 2
TERMINAL_ENTRY 3
TERMINAL_ENTRY 4
TERMINAL_ENTRY 5
TERMINAL_ENTRY 6
TERMINAL_ENTRY 7
TERMINAL_ENTRY 8
TERMINAL_ENTRY 9
TERMINAL_ENTRY 10
TERMINAL_ENTRY 11
TERMINAL_ENTRY 12
TERMINAL_ENTRY 13
TERMINAL_ENTRY 14
TERMINAL_ENTRY 15
TERMINAL_ENTRY 16
TERMINAL_ENTRY 17
TERMINAL_ENTRY 18
TERMINAL_ENTRY 19
TERMINAL_ENTRY 20
TERMINAL_ENTRY 21
TERMINAL_ENTRY 22
TERMINAL_ENTRY 23
TERMINAL_ENTRY 24
TERMINAL_ENTRY 25
TERMINAL_ENTRY 26
TERMINAL_ENTRY 27
TERMINAL_ENTRY 28
TERMINAL_ENTRY 29
TERMINAL_ENTRY 30
TERMINAL_ENTRY 31

; ====== arresto con vettore in RAX e frame CPU conservato ====== ;

PUBLIC x86_boot_exception_stop
x86_boot_exception_stop PROC
    cli
halt_again:
    hlt
    jmp halt_again
x86_boot_exception_stop ENDP

; ====== indirizzi rilocabili separati dal codice eseguibile ====== ;

.const
ALIGN 8
PUBLIC x86_boot_exception_targets
x86_boot_exception_targets LABEL QWORD
IRP vector, <0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31>
    DQ x86_exception_&vector
ENDM
END

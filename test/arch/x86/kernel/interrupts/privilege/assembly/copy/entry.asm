option casemap:none
EXTERN k5_negative:QWORD
EXTERN k5_nmi_count:QWORD
EXTERN k5_copy_wait:QWORD
EXTERN k5_copy_budget:QWORD
EXTERN x86_k5_stop:PROC
EXTERN x86_k5_nmi:PROC
EXTERN x86_k5_pf:PROC
EXTERN x86_k5_copy_recover:PROC

.code

; ====== copia con AC aperto soltanto attorno a una istruzione ======

; Intel SDM 2B, STAC e CLAC; Intel SDM 3A §5.6.1: con SMAP l'accesso esplicito a una
; pagina utente da supervisor richiede EFLAGS.AC=1. LFENCE chiude la speculazione sul range.
PUBLIC x86_k5_copy_raw, x86_k5_copy_wait_first, x86_k5_copy_wait_last
PUBLIC x86_k5_copy_first, x86_k5_copy_last, x86_k5_copy_fixup
x86_k5_copy_raw PROC
    push rdi
    push rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    cld
    lfence
    cmp QWORD PTR [k5_negative], 20
    je copy_open
    stac
copy_open:
    cmp QWORD PTR [k5_copy_wait], 0
    je NEAR PTR x86_k5_copy_first
x86_k5_copy_wait_first LABEL BYTE
    pause
    cmp QWORD PTR [k5_nmi_count], 1
    je copy_wait_done
    dec QWORD PTR [k5_copy_budget]
    jnz NEAR PTR x86_k5_copy_wait_first
x86_k5_copy_wait_last LABEL BYTE
    jmp x86_k5_stop
copy_wait_done:
    mov QWORD PTR [k5_copy_wait], 0
x86_k5_copy_first LABEL BYTE
    rep movsb
x86_k5_copy_last LABEL BYTE
    cmp QWORD PTR [k5_negative], 25
    je copy_done
    clac
copy_done:
    xor eax, eax
    pop rsi
    pop rdi
    ret
x86_k5_copy_fixup LABEL BYTE
    clac
    mov eax, 1
    pop rsi
    pop rdi
    ret
x86_k5_copy_raw ENDP

; ====== NMI con AC chiuso prima di qualsiasi accesso ======

; Intel SDM 3A §7.12: la consegna azzera TF, VM, RF e NT, non AC. IRETQ ripristina AC.
PUBLIC x86_k5_copy_nmi
x86_k5_copy_nmi PROC
    clac
    cmp QWORD PTR [k5_copy_wait], 0
    je x86_k5_stop
    test DWORD PTR [rsp+16], 40000h
    jz x86_k5_stop
    jmp x86_k5_nmi
x86_k5_copy_nmi ENDP

; ====== #PF recuperabile soltanto nella finestra di copia ======

; Intel SDM 3A §§7.14.2, 7.15: error code, RIP, CS, RFLAGS, RSP, SS su IST1.
PUBLIC x86_k5_copy_pf
x86_k5_copy_pf PROC
    clac
    cmp QWORD PTR [rsp+16], 33h
    je x86_k5_pf
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    lea rcx, [rsp+56]
    mov rdx, cr2
    sub rsp, 40
    call x86_k5_copy_recover
    add rsp, 40
    cmp eax, 1
    jne x86_k5_stop
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    add rsp, 8
    iretq
x86_k5_copy_pf ENDP

; ====== chiamata utente con risultato atteso ======

K5_CALL MACRO number, pointer, bytes, expected
    mov eax, number
    mov rdi, pointer
    mov rsi, bytes
    syscall
    mov rdx, expected
    cmp rax, rdx
    jne NEAR PTR x86_k5_copy_fail
ENDM

; ====== payload di copia in una pagina utente RX ======

PUBLIC x86_k5_copy_user, x86_k5_copy_user_end, x86_k5_copy_ok, x86_k5_copy_fail
x86_k5_copy_user LABEL BYTE
    K5_CALL 3, 405000h, 16, 0
    K5_CALL 2, 405000h, 16, 2680
    K5_CALL 2, 405000h, 0, 0
    K5_CALL 3, 405000h, 256, 0
    xor ecx, ecx
    mov r8, 405000h
user_verify:
    mov eax, ecx
    xor eax, 0a5h
    cmp BYTE PTR [r8+rcx], al
    jne NEAR PTR x86_k5_copy_fail
    inc ecx
    cmp ecx, 256
    jne user_verify
    K5_CALL 2, 405000h, 256, 32640
    mov r8, 405ff8h
    mov r9, 0101010101010101h
    mov QWORD PTR [r8], r9
    K5_CALL 2, 405ff8h, 16, 728
    K5_CALL 2, 401000h, 8, -2
    K5_CALL 3, 406000h, 8, -2
    K5_CALL 2, 400ff0h, 32, -2
    K5_CALL 3, 405ff0h, 32, -2
    mov r8, 405ff0h
    cmp BYTE PTR [r8], 0a5h
    jne NEAR PTR x86_k5_copy_fail
    K5_CALL 2, 408000h, 8, -2
    K5_CALL 3, 408000h, 8, -2
    K5_CALL 2, 0ffffffff80000000h, 8, -2
    K5_CALL 2, 0000800000000000h, 8, -2
    K5_CALL 2, 0fffffffffffffff8h, 16, -2
    K5_CALL 2, 405000h, 257, -3
x86_k5_copy_ok LABEL BYTE
    ud2
x86_k5_copy_fail LABEL BYTE
    ud2
x86_k5_copy_user_end LABEL BYTE
END

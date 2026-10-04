option casemap:none
.code

; ====== IF uno soltanto nella prova delimitata senza accessi RAM concorrenti ===== ;

PUBLIC x86_irq_wait
PUBLIC x86_uart_irq_wait
x86_irq_wait LABEL NEAR
x86_uart_irq_wait PROC
    pushfq
    pop rax
    test eax, 200h
    jnz wait_bad
    test r8d, r8d
    jz wait_bad
    sti
    nop
wait_poll:
    cmp DWORD PTR [rcx+4], 0
    jne wait_close_bad
    cmp DWORD PTR [rcx], edx
    jae wait_good
    pause
    dec r8d
    jnz wait_poll
wait_close_bad:
    cli
wait_bad:
    xor eax, eax
    ret
wait_good:
    cli
    mov eax, 1
    ret
x86_uart_irq_wait ENDP

; ====== evento gia consegnato oppure sonno atomico rispetto agli IRQ maskable ===== ;

PUBLIC x86_irq_sleep
x86_irq_sleep PROC
    pushfq
    pop rax
    test eax, 200h
    jnz sleep_bad
    test rcx, rcx
    jz sleep_bad
    test rcx, 7
    jnz sleep_bad
    test edx, edx
    jz sleep_bad
    test r8d, r8d
    jz sleep_bad
    cmp r8d, 1024
    ja sleep_bad
    xor r9d, r9d
sleep_check:
    cmp DWORD PTR [rcx+4], 0
    jne sleep_bad
    cmp DWORD PTR [rcx], edx
    jae sleep_done
    cmp r9d, r8d
    jae sleep_bad
    ; IF=0 fino a STI; nessuna istruzione tra STI e HLT.
    sti
    hlt
sleep_resume::
    cli
    inc r9d
    jmp sleep_check
sleep_done:
    lea eax, [r9+1]
    ret
sleep_bad:
    xor eax, eax
    ret
x86_irq_sleep ENDP

; ====== indirizzo del ritorno HLT per verifica indipendente della traccia IRQ ===== ;

PUBLIC x86_irq_sleep_resume
x86_irq_sleep_resume PROC
    lea rax, sleep_resume
    ret
x86_irq_sleep_resume ENDP
END

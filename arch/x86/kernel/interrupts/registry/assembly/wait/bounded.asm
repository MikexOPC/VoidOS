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
END

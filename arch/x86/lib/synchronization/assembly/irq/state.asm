option casemap:none
.code

; ====== ks_irq_save ======

; Intel SDM 253668-090US 2.3/7.8.1: RFLAGS.IF bit 9, CLI masks local maskable IRQ.
; Capture before CLI so nesting restores precisely the caller's IF state. No other flags restored.
PUBLIC ks_irq_save
ks_irq_save PROC
    pushfq
    pop rax
    cli
    and eax, 200h
    ret
ks_irq_save ENDP

; ====== ks_irq_state ======

; Intel SDM 253668-090US 2.3: observe IF bit 9 without modifying interrupt enable state.
PUBLIC ks_irq_state
ks_irq_state PROC
    pushfq
    pop rax
    and eax, 200h
    ret
ks_irq_state ENDP

; ====== ks_irq_restore ======

; Intel SDM 253668-090US 7.8.1: STI/CLI affect IF; STI defers IRQ delivery through the following RET.
; Microsoft x64 RCX = validated saved IF (0 or 200h). Other RFLAGS fields remain untouched.
PUBLIC ks_irq_restore
ks_irq_restore PROC
    test ecx, 200h
    jz restore_disabled
    sti
    ret
restore_disabled:
    cli
    ret
ks_irq_restore ENDP

; ====== ks_pause ======

; Intel SDM 253668-090US 11.10.2/11.10.6.1: PAUSE is a spin hint, not a memory barrier.
PUBLIC ks_pause
ks_pause PROC
    pause
    ret
ks_pause ENDP
END

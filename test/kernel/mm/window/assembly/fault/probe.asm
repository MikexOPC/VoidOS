option casemap:none
.code

; ====== accessi intenzionalmente vietati solo nelle immagini di test ====== ;

PUBLIC x86_window_fault
x86_window_fault PROC
    cmp edx, 1
    je execute_page
    mov al, BYTE PTR [rcx]
    ret
execute_page:
    jmp rcx
x86_window_fault ENDP
END

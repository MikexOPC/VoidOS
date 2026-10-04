option casemap:none
.code

; ====== PM timer ACPI letto in dword da porta gia validata dal chiamante ===== ;

PUBLIC x86_pm_read
x86_pm_read PROC
    mov edx, ecx
    in eax, dx
    ret
x86_pm_read ENDP
END

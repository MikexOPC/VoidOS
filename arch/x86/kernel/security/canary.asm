; Microsoft x64 /GS ABI: __security_check_cookie receives the recovered cookie in RCX
; and must preserve RAX (caller return value) and all non-volatile registers.
; Intel SDM Vol. 2B: CMP, ROL, ROR, TEST, Jcc, RET.

extern __security_cookie : qword
extern __report_gsfailure : proc

.code

; ====== __security_check_cookie ======

public __security_check_cookie
__security_check_cookie proc
    cmp rcx, qword ptr [__security_cookie]
    jne gs_fail
    rol rcx, 16
    test cx, cx
    jne gs_fail_restore
    ret
gs_fail_restore:
    ror rcx, 16
gs_fail:
    jmp __report_gsfailure
__security_check_cookie endp

end

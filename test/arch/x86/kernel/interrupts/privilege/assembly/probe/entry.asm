option casemap:none
EXTERN k5_owner:QWORD
EXTERN k5_phase:QWORD
EXTERN k5_first:QWORD
EXTERN k5_last:QWORD
EXTERN k5_nmi_count:QWORD
EXTERN k5_nmi_rip:QWORD
EXTERN k5_nmi_gs:QWORD
EXTERN k5_nmi_stack:QWORD
EXTERN k5_fault:QWORD
EXTERN k5_fault_stack:QWORD
EXTERN k5_cr2:QWORD
EXTERN k5_live_root:QWORD
EXTERN k5_fx_clean:BYTE
EXTERN k5_fx_saved:BYTE
EXTERN k5_stack:QWORD
EXTERN k5_root:QWORD

.data
ALIGN 8
k5_kernel_rsp QWORD 0
k5_kernel_root QWORD 0
k5_kernel_cr2 QWORD 0

.code

; ====== GDT diagnostica con il descrittore TSS corrente conservato ======

PUBLIC x86_k5_load_gdt
x86_k5_load_gdt PROC
    lgdt FWORD PTR [rcx]
    ret
x86_k5_load_gdt ENDP

; ====== selettori senza LDT e con segmenti FS e GS nulli ======

; Intel SDM 3A §§3.4.2-4 e vol. 2, SLDT: nessuna tabella locale attiva.
PUBLIC x86_k5_selectors
x86_k5_selectors PROC
    sldt ax
    test ax, ax
    jnz bad_selectors
    mov ax, fs
    test ax, ax
    jnz bad_selectors
    mov ax, gs
    test ax, ax
    jnz bad_selectors
    mov ax, ds
    cmp ax, 16
    jne bad_selectors
    mov ax, es
    cmp ax, 16
    jne bad_selectors
    mov eax, 1
    ret
bad_selectors:
    xor eax, eax
    ret
x86_k5_selectors ENDP

; ====== coppia GS attestata prima di ogni accesso per CPU ======

; Intel SDM Vol. 4 Table 2-2: C0000101h base attiva, C0000102h destinazione SWAPGS.
; Vol. 2, SWAPGS: lo scambio avviene a CPL0 senza accedere allo stack utente.
K5_KERNEL_GS MACRO
    LOCAL kernel_gs, verify_gs
    mov ecx, 0c0000101h
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov r11, rax
    mov ecx, 0c0000102h
    rdmsr
    shl rdx, 32
    or rax, rdx
    xor r10d, r10d
    cmp r11, QWORD PTR [k5_owner]
    je kernel_gs
    test r11, r11
    jnz x86_k5_stop
    cmp rax, QWORD PTR [k5_owner]
    jne x86_k5_stop
    swapgs
    inc r10d
    jmp verify_gs
kernel_gs:
    test rax, rax
    jnz x86_k5_stop
verify_gs:
    lfence
    mov rax, QWORD PTR gs:[0]
    cmp rax, QWORD PTR [k5_owner]
    jne x86_k5_stop
ENDM

; ====== NMI con stack IST2 e ritorno nella fase interrotta ======

; Intel SDM 3A §§7.14.2, 7.14.5: il frame contiene RIP, CS, RFLAGS, RSP, SS.
PUBLIC x86_k5_nmi
x86_k5_nmi PROC
    mov [k5_nmi_stack], rsp
    push rax
    push rcx
    push rdx
    push r10
    push r11
    cmp QWORD PTR [k5_nmi_count], 0
    jne x86_k5_stop
    mov rax, [rsp+40]
    cmp rax, QWORD PTR [k5_first]
    jb x86_k5_stop
    cmp rax, QWORD PTR [k5_last]
    jae x86_k5_stop
    mov [k5_nmi_rip], rax
    K5_KERNEL_GS
    mov [k5_nmi_gs], r11
    cmp QWORD PTR [k5_phase], 3
    jne nmi_kernel
    cmp QWORD PTR [rsp+48], 33h
    jne x86_k5_stop
    test r10d, r10d
    jz x86_k5_stop
    mov QWORD PTR [rsp+32], 1
    jmp nmi_finish
nmi_kernel:
    cmp QWORD PTR [rsp+48], 8
    jne x86_k5_stop
    mov rax, [k5_phase]
    dec rax
    cmp rax, r10
    jne x86_k5_stop
nmi_finish:
    mov QWORD PTR [k5_nmi_count], 1
    test r10d, r10d
    jz nmi_restore
    swapgs
nmi_restore:
    pop r11
    pop r10
    pop rdx
    pop rcx
    pop rax
    iretq
x86_k5_nmi ENDP

; ====== finestra controllata CPL0 prima e dopo SWAPGS ======

PUBLIC x86_k5_wait, x86_k5_wait_first, x86_k5_wait_last
x86_k5_wait PROC
    test ecx, ecx
    jz wait_setup
    swapgs
wait_setup:
    mov r9d, 4000000h
    mov r8d, 71bc2e16h
x86_k5_wait_first LABEL BYTE
    pause
    cmp QWORD PTR [k5_nmi_count], 1
    je wait_done
    dec r9d
    jnz NEAR PTR x86_k5_wait_first
x86_k5_wait_last LABEL BYTE
    jmp x86_k5_stop
wait_done:
    cmp r8d, 71bc2e16h
    jne x86_k5_stop
    test ecx, ecx
    jz wait_return
    swapgs
wait_return:
    ret
x86_k5_wait ENDP

; ====== ingresso CPL3 con frame sintetico e stato SIMD inizializzato ======

; Intel SDM 3A §§3.4.2, 7.14.3: selettori RPL3, frame IRETQ a cinque slot.
; Vol. 2, FXSAVE/FXRSTOR: immagine di 512 byte allineata a 16.
PUBLIC x86_k5_enter
x86_k5_enter PROC
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov [k5_kernel_rsp], rsp
    mov rax, cr3
    mov [k5_kernel_root], rax
    mov rax, cr2
    mov [k5_kernel_cr2], rax
    fxsave64 [k5_fx_saved]
    fxrstor64 [k5_fx_clean]
    mov rax, [k5_root]
    mov cr3, rax
    push 2bh
    push QWORD PTR [k5_stack]
    push 2
    push 33h
    push rcx
    mov edi, edx
    mov rsi, r8
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    swapgs
    iretq
x86_k5_enter ENDP

; ====== cattura dei soli fault attesi su IST1 ======

PUBLIC x86_k5_ud, x86_k5_gp, x86_k5_pf
x86_k5_ud LABEL BYTE
    push 0
    push 6
    jmp k5_fault_entry
x86_k5_gp LABEL BYTE
    push 13
    jmp k5_fault_entry
x86_k5_pf LABEL BYTE
    push 14
k5_fault_entry:
    mov [k5_fault_stack], rsp
    cmp QWORD PTR [rsp+24], 33h
    jne x86_k5_stop
    cmp QWORD PTR [k5_kernel_rsp], 0
    je x86_k5_stop
    K5_KERNEL_GS
    test r10d, r10d
    jz x86_k5_stop
    cld
    mov rsi, rsp
    lea rdi, k5_fault
    mov ecx, 7
    rep movsq
    mov rax, cr2
    mov [k5_cr2], rax
    mov rax, cr3
    mov [k5_live_root], rax
    mov rax, [k5_kernel_root]
    mov cr3, rax
    mov rax, [k5_kernel_cr2]
    mov cr2, rax
    mov rsp, [k5_kernel_rsp]
    mov QWORD PTR [k5_kernel_rsp], 0
    fxrstor64 [k5_fx_saved]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    mov ax, 16
    mov ss, ax
    mov ds, ax
    mov es, ax
    ret

; ====== arresto senza affidarsi a GS per stato non attestato ======

PUBLIC x86_k5_stop
x86_k5_stop PROC
    cli
    mov eax, 4b354641h
k5_stopped:
    hlt
    jmp k5_stopped
x86_k5_stop ENDP

; ====== payload copiato in una pagina utente RX ======

PUBLIC x86_k5_user, x86_k5_user_end, x86_k5_user_first, x86_k5_user_last
PUBLIC x86_k5_user_cli, x86_k5_user_read, x86_k5_user_ud
x86_k5_user LABEL BYTE
    cmp edi, 1
    je NEAR PTR x86_k5_user_cli
    cmp edi, 2
    je NEAR PTR x86_k5_user_read
    cmp edi, 3
    jne user_wait_setup
    mov BYTE PTR [rsp-8], 0cch
    lea rax, [rsp-8]
    jmp rax
x86_k5_user_cli LABEL BYTE
    cli
    ud2
x86_k5_user_read LABEL BYTE
    mov rax, [rsi]
    ud2
user_wait_setup:
    mov ecx, 4000000h
    mov r8d, 71bc2e16h
x86_k5_user_first LABEL BYTE
    pause
    test eax, eax
    jnz user_resumed
    dec ecx
    jnz NEAR PTR x86_k5_user_first
x86_k5_user_last LABEL BYTE
    ud2
user_resumed:
    cmp eax, 1
    jne NEAR PTR x86_k5_user_last
    cmp r8d, 71bc2e16h
    jne NEAR PTR x86_k5_user_last
x86_k5_user_ud LABEL BYTE
    ud2
x86_k5_user_end LABEL BYTE
END

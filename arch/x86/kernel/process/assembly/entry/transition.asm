option casemap:none
EXTERN k6_owner:QWORD
EXTERN k6_kernel_root:QWORD
EXTERN k6_user_root:QWORD
EXTERN k6_stack_top:QWORD
EXTERN k6_user_rsp:QWORD
EXTERN k6_saved_rsp:QWORD
IFDEF K6_DIAGNOSTICS
EXTERN k6_window:QWORD
EXTERN k6_window_budget:QWORD
EXTERN k6_negative:QWORD
EXTERN k6_nmi_armed:QWORD
EXTERN k6_nmi_count:QWORD
EXTERN k6_nmi_rip:QWORD
EXTERN k6_nmi_cs:QWORD
EXTERN k6_nmi_cr3:QWORD
EXTERN k6_nmi_gs:QWORD
EXTERN k6_nmi_flags:QWORD
ENDIF
EXTERN k6_drain:QWORD
EXTERN k6_fault:QWORD
EXTERN k6_fault_cr2:QWORD
EXTERN k6_exit_kind:QWORD
EXTERN k6_fx_clean:BYTE
EXTERN k6_fx_saved:BYTE
EXTERN x86_process_syscall_dispatch:PROC
EXTERN x86_process_irq_service:PROC
EXTERN x86_process_irq_drained:PROC
IFDEF K6_DIAGNOSTICS
EXTERN x86_process_device_irq_service:PROC
EXTERN x86_process_device_irq_kernel:PROC
ENDIF
EXTERN x86_process_copy_recover:PROC
EXTERN x86_process_kernel_fatal:PROC

K6_EXIT_FAULT EQU 2

.code

; ====== attesa diagnostica senza registri, stack o GS ======

K6_WAIT MACRO number
    LOCAL skip, done
IFDEF K6_DIAGNOSTICS
    cmp QWORD PTR [k6_window], number
    jne skip
PUBLIC x86_process_window_&number&_first, x86_process_window_&number&_last
x86_process_window_&number&_first LABEL BYTE
    pause
    cmp QWORD PTR [k6_nmi_count], 1
    je done
    dec QWORD PTR [k6_window_budget]
    jnz NEAR PTR x86_process_window_&number&_first
x86_process_window_&number&_last LABEL BYTE
    jmp x86_process_stop
done:
    mov QWORD PTR [k6_window], 0
skip:
ENDIF
ENDM

K6_PUSH_GPR MACRO
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
ENDM

K6_POP_GPR MACRO
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
ENDM

; ====== x86_process_stop ======

PUBLIC x86_process_stop
x86_process_stop PROC
    cli
    mov eax, 4b354641h
stopped:
    hlt
    jmp stopped
x86_process_stop ENDP

; ====== x86_process_syscall ======

; Intel SDM 2B, SYSCALL: RCX = RIP successivo, R11 = RFLAGS, RSP non salvato.
; Intel SDM 3A §6.8.8: CS, SS da IA32_STAR, RIP da IA32_LSTAR, flag da IA32_FMASK.
; Intel SDM 2B, SWAPGS: scambio fra C0000101h e C0000102h, lecito a CPL0.
; Contratto delle due root: CR3 del kernel solo dopo il cambio a uno stack mappato in entrambe.
PUBLIC x86_process_syscall
x86_process_syscall PROC
    K6_WAIT 1
IFDEF K6_DIAGNOSTICS
    cmp QWORD PTR [k6_negative], 35
    je syscall_swapped
ENDIF
    swapgs
syscall_swapped:
    lfence
    K6_WAIT 2
    mov [k6_user_rsp], rsp
    mov rsp, [k6_stack_top]
    push 2bh
    push QWORD PTR [k6_user_rsp]
    push r11
    push 33h
    push rcx
    K6_PUSH_GPR
    mov rax, [k6_kernel_root]
    mov cr3, rax
    K6_WAIT 3
    sub rsp, 512
    fxsave64 [rsp]
    fxrstor64 [k6_fx_clean]
    cld
    mov ecx, 0c0000101h
    rdmsr
    shl rdx, 32
    or rax, rdx
    cmp rax, QWORD PTR [k6_owner]
    jne x86_process_stop
    mov rax, QWORD PTR gs:[0]
    cmp rax, QWORD PTR [k6_owner]
    jne x86_process_stop
    mov rbx, rsp
    mov rcx, rsp
    sub rsp, 32
    call x86_process_syscall_dispatch
    mov rsp, rbx
    cmp eax, 1
    je syscall_return
    cmp eax, 2
    je x86_process_leave
    jmp x86_process_stop
syscall_return:
    fxrstor64 [rsp]
    add rsp, 512
    K6_WAIT 4
    mov rax, [k6_user_root]
    mov cr3, rax
    K6_WAIT 5
    swapgs
    K6_POP_GPR
    iretq
x86_process_syscall ENDP

; ====== x86_process_enter ======

; Intel SDM 3A §§3.4.2, 7.14.3: selettori RPL3, frame IRETQ a cinque slot, IF=1 nell'immagine.
; Microsoft x64 ABI: RCX = entry, RDX = stack, R8 = argomento iniziale per RDI.
PUBLIC x86_process_enter
x86_process_enter PROC
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov [k6_saved_rsp], rsp
    fxsave64 [k6_fx_saved]
    fxrstor64 [k6_fx_clean]
    push 2bh
    push rdx
    push 202h
    push 33h
    push rcx
    mov rax, [k6_user_root]
    mov cr3, rax
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor ebp, ebp
    xor esi, esi
    mov rdi, r8
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
x86_process_enter ENDP

IFDEF K6_DIAGNOSTICS
; ====== x86_process_resume ======

; Intel SDM 3A §7.14.3: frame IRETQ a cinque slot. Vol. 2 FXRSTOR64: area da 512 byte allineata.
PUBLIC x86_process_resume
x86_process_resume PROC
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    mov [k6_saved_rsp], rsp
    fxsave64 [k6_fx_saved]
    fxrstor64 [rcx]
    sub rsp, 160
    lea rsi, [rcx+512]
    mov rdi, rsp
    mov ecx, 20
    cld
    rep movsq
    mov rax, [k6_user_root]
    mov cr3, rax
    swapgs
    K6_POP_GPR
    iretq
x86_process_resume ENDP
ENDIF

; ====== x86_process_leave ======

; Root e GS del kernel gia attivi. Segmenti utente azzerati senza toccare la base GS del kernel.
x86_process_leave PROC
    mov rsp, [k6_saved_rsp]
    mov ax, 16
    mov ss, ax
    mov ds, ax
    mov es, ax
    xor eax, eax
    mov fs, ax
    swapgs
    mov gs, ax
    swapgs
    fxrstor64 [k6_fx_saved]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
x86_process_leave ENDP

; ====== ingresso e ritorno comuni per IRQ da CPL3 ======

; Intel SDM 3A §§7.14.2-7.14.4: con IST=0 il frame CPL3 usa TSS.RSP0.
; IRETQ ripristina RIP, CS, RFLAGS, RSP e SS dal frame.
K6_USER_IRQ MACRO service
    swapgs
    lfence
    K6_PUSH_GPR
    mov rax, [k6_kernel_root]
    mov cr3, rax
    ; Intel SDM 3A §7.14.4: il cambio di privilegio carica SS nullo. I validatori del LAPIC
    ; richiedono il selettore del kernel. IRETQ riprende SS utente dal frame.
    mov ax, 16
    mov ss, ax
    sub rsp, 512
    fxsave64 [rsp]
    fxrstor64 [k6_fx_clean]
    cld
    mov rbx, rsp
    mov rcx, rsp
    sub rsp, 32
    call service
    mov rsp, rbx
    cmp eax, 2
    je x86_process_leave
    cmp eax, 1
    jne x86_process_stop
    fxrstor64 [rsp]
    add rsp, 512
    mov rax, [k6_user_root]
    mov cr3, rax
    swapgs
    K6_POP_GPR
    iretq
ENDM

; ====== IRQ del timer da CPL3 sullo stack RSP0 ======

; Intel SDM 3A §7.12: CLAC rimuove AC prima di usare la memoria.
; Il CS salvato è a RSP+8 e RPL occupa i bit 1:0.
PUBLIC x86_process_irq
x86_process_irq PROC
    clac
    test BYTE PTR [rsp+8], 3
    jz irq_kernel
    K6_USER_IRQ x86_process_irq_service
irq_kernel:
    cmp QWORD PTR [k6_drain], 1
    jne irq_kernel_stray
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    cld
    sub rsp, 32
    call x86_process_irq_drained
    add rsp, 32
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    iretq
irq_kernel_stray:
    push 0
    push 0ffffh
    jmp fault_fatal
x86_process_irq ENDP

IFDEF K6_DIAGNOSTICS
; ====== IRQ diagnostico registrato da CPL3 o durante il drenaggio CPL0 ======

PUBLIC x86_process_device_irq
x86_process_device_irq PROC
    clac
    test BYTE PTR [rsp+8], 3
    jz device_irq_kernel
    K6_USER_IRQ x86_process_device_irq_service
device_irq_kernel:
    cmp QWORD PTR [k6_drain], 1
    jne x86_process_stray
    K6_PUSH_GPR
    mov rbx, rsp
    and rsp, -16
    sub rsp, 544
    fxsave64 [rsp+32]
    cld
    call x86_process_device_irq_kernel
    fxrstor64 [rsp+32]
    mov rsp, rbx
    K6_POP_GPR
    iretq
x86_process_device_irq ENDP
ENDIF

; ====== vettori esterni inattesi ======

; Nessuna origine attesa durante un processo: arresto con diagnosi dalla root del kernel.
PUBLIC x86_process_stray
x86_process_stray PROC
    clac
    push 0
    push 0ffffh
    jmp fault_fatal
x86_process_stray ENDP

; ====== spurious del LAPIC ======

; Intel SDM 3A, Local APIC, Handling Spurious Interrupts: nessun bit ISR, nessun EOI.
PUBLIC x86_process_spurious
x86_process_spurious PROC
    iretq
x86_process_spurious ENDP

; ====== NMI senza presupposti su CR3, GS o AC ======

; Intel SDM 3A §7.12: la consegna azzera TF, VM, RF e NT, non AC. IRETQ ripristina AC e flag.
; Intel SDM 3A §7.14.5: stack IST2. Stato salvato e rimesso esattamente prima del ritorno.
PUBLIC x86_process_nmi
x86_process_nmi PROC
    clac
    push rax
    push rcx
    push rdx
    push r10
    push r11
    mov rax, cr3
    push rax
    mov rax, cr2
    push rax
    xor r10d, r10d
    mov ecx, 0c0000101h
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov r11, rax
    cmp rax, QWORD PTR [k6_owner]
    je nmi_gs_ready
    mov ecx, 0c0000102h
    rdmsr
    shl rdx, 32
    or rax, rdx
    cmp rax, QWORD PTR [k6_owner]
    jne x86_process_stop
    swapgs
    inc r10d
nmi_gs_ready:
    lfence
    mov rax, [k6_kernel_root]
    mov cr3, rax
IFDEF K6_DIAGNOSTICS
    cmp QWORD PTR [k6_nmi_armed], 1
    jne nmi_stray
    cmp QWORD PTR [k6_nmi_count], 0
    jne nmi_stray
    mov rax, [rsp+56]
    mov [k6_nmi_rip], rax
    mov rax, [rsp+64]
    mov [k6_nmi_cs], rax
    mov rax, [rsp+72]
    mov [k6_nmi_flags], rax
    mov rax, [rsp+8]
    mov [k6_nmi_cr3], rax
    mov [k6_nmi_gs], r11
    mov QWORD PTR [k6_nmi_count], 1
    pop rax
    mov cr2, rax
    pop rax
    mov cr3, rax
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
nmi_stray:
ENDIF
    add rsp, 56
    push 0
    push 2
    jmp fault_fatal
x86_process_nmi ENDP

; ====== fault con frame uniforme vettore, errore, RIP, CS, RFLAGS, RSP, SS ======

K6_FAULT MACRO vector, cpu_error
x86_process_fault_&vector LABEL BYTE
    IF cpu_error EQ 0
        push 0
    ENDIF
    push vector
    jmp NEAR PTR fault_common
ENDM

PUBLIC x86_process_faults_begin
x86_process_faults_begin LABEL BYTE
IRP vector, <0,1,3,4,5,6,7,9,15,16,19,20,22,23,24,25,26,27,28,31>
    K6_FAULT vector, 0
ENDM
IRP vector, <10,11,12,13,14,17,21,29,30>
    K6_FAULT vector, 1
ENDM

; Intel SDM 3A §7.15: da CPL3 il processo termina; da CPL0 recupero solo nella finestra di copia.
fault_common LABEL BYTE
    clac
    test BYTE PTR [rsp+24], 3
    jz fault_kernel
    swapgs
    lfence
    mov rax, [k6_kernel_root]
    mov cr3, rax
    cld
    mov rsi, rsp
    lea rdi, k6_fault
    mov ecx, 7
    rep movsq
    mov rax, cr2
    mov [k6_fault_cr2], rax
    mov QWORD PTR [k6_exit_kind], K6_EXIT_FAULT
    jmp x86_process_leave
fault_kernel:
    cmp QWORD PTR [rsp], 14
    jne fault_fatal
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11
    lea rcx, [rsp+56]
    mov rdx, cr2
    sub rsp, 32
    call x86_process_copy_recover
    add rsp, 32
    cmp eax, 1
    jne fault_fatal_saved
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    add rsp, 16
    iretq
fault_fatal_saved:
    add rsp, 56
fault_fatal:
    mov rax, [k6_kernel_root]
    mov cr3, rax
    mov ecx, 0c0000101h
    rdmsr
    shl rdx, 32
    or rax, rdx
    cmp rax, QWORD PTR [k6_owner]
    je fault_gs_ready
    swapgs
fault_gs_ready:
    lfence
    mov rcx, rsp
    mov rdx, cr2
    and rsp, -16
    sub rsp, 32
    call x86_process_kernel_fatal
    jmp x86_process_stop
PUBLIC x86_process_faults_end
x86_process_faults_end LABEL BYTE

; ====== x86_process_copy ======

; Intel SDM 2B, STAC e CLAC; §5.6.1 di 3A: accesso esplicito a pagina utente con AC=1.
; IF=0: la root del processo e AC aperto restano attivi solo per questa istruzione e per un NMI.
PUBLIC x86_process_copy, x86_process_copy_first, x86_process_copy_last, x86_process_copy_fixup
x86_process_copy PROC
    push rdi
    push rsi
    mov rdi, rcx
    mov rsi, rdx
    mov rcx, r8
    cld
    lfence
    mov rax, [k6_user_root]
    mov cr3, rax
    stac
    K6_WAIT 6
x86_process_copy_first LABEL BYTE
    rep movsb
x86_process_copy_last LABEL BYTE
    clac
    mov rax, [k6_kernel_root]
    mov cr3, rax
    xor eax, eax
    pop rsi
    pop rdi
    ret
x86_process_copy_fixup LABEL BYTE
    clac
    mov rax, [k6_kernel_root]
    mov cr3, rax
    mov eax, 1
    pop rsi
    pop rdi
    ret
x86_process_copy ENDP

IFDEF K6_DIAGNOSTICS
; ====== x86_process_probe_read ======

; Solo prova negativa: lettura di un indirizzo con la root del processo attiva.
PUBLIC x86_process_probe_read
x86_process_probe_read PROC
    mov rax, [k6_user_root]
    mov cr3, rax
    movzx eax, BYTE PTR [rcx]
    mov rax, [k6_kernel_root]
    mov cr3, rax
    ret
x86_process_probe_read ENDP

; ====== x86_process_spurious_check ======

; Solo prova: il gate spurious torna senza EOI e senza toccare stato.
PUBLIC x86_process_spurious_check
x86_process_spurious_check PROC
    int 0ffh
    ret
x86_process_spurious_check ENDP
ENDIF

; ====== indirizzi dei gate in memoria non eseguibile ======

K6_TARGET MACRO vector
    DQ x86_process_fault_&vector
ENDM
.const
ALIGN 8
PUBLIC x86_process_fault_targets
x86_process_fault_targets LABEL QWORD
DQ x86_process_fault_0, x86_process_fault_1, 0, x86_process_fault_3
DQ x86_process_fault_4, x86_process_fault_5, x86_process_fault_6, x86_process_fault_7
DQ 0, x86_process_fault_9, x86_process_fault_10, x86_process_fault_11
DQ x86_process_fault_12, x86_process_fault_13, x86_process_fault_14, x86_process_fault_15
DQ x86_process_fault_16, x86_process_fault_17, 0, x86_process_fault_19
DQ x86_process_fault_20, x86_process_fault_21, x86_process_fault_22, x86_process_fault_23
DQ x86_process_fault_24, x86_process_fault_25, x86_process_fault_26, x86_process_fault_27
DQ x86_process_fault_28, x86_process_fault_29, x86_process_fault_30, x86_process_fault_31
END

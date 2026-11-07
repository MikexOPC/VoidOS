; ====== checked_service_idle ======

; Intel SDM 078US 6.8.1: STI delays maskable interrupts through the next
; instruction; contiguous STI/HLT avoids the idle lost-wakeup window.
; 6.14: IRETQ restores DF/IF. An interrupt gate clears IF; entry clears DF
; for the Microsoft ABI. CLI below closes the maskable interrupt window.
; Intel SDM 253666-089US Vol.2A FXSAVE Table3-48: XMM0-15 at160..415,
; MXCSR24, ST0 at32. FNINIT/FLD1 produce FCW037f, TOP7 and FTW80.
bits 64
default rel
section .text
global xis_service_loop
%ifdef KERNEL_TEST_AP_SERVICE
extern xis_native_verified, xis_native_abort
%ifdef KERNEL_AP_ADMISSION
extern xad_native_poll
%endif

; ====== xis_service_loop ======

xis_service_loop:
    sub rsp, 576
    mov qword [rsp+32], 1
    jmp .pattern

; ====== register_pattern ======

.pattern:
    fninit
    fld1
    pcmpeqd xmm0, xmm0
%assign i 1
%rep 15
    movdqa xmm%+i, xmm0
%assign i i+1
%endrep
%macro pattern 2
    mov %1, %2
%endmacro
    pattern rax, 0x12345001
    pattern rbx, 0x12345002
    pattern rcx, 0x12345003
    pattern rdx, 0x12345004
    pattern rbp, 0x12345005
    pattern rsi, 0x12345006
    pattern rdi, 0x12345007
    pattern r8, 0x12345008
    pattern r9, 0x12345009
    pattern r10, 0x1234500a
    pattern r11, 0x1234500b
    pattern r12, 0x1234500c
    pattern r13, 0x1234500d
    pattern r14, 0x1234500e
    pattern r15, 0x1234500f
    std

; ====== sleeping_with_interrupts_enabled ======

    cmp qword [rsp+32], 1
    jne .sleep
    mov qword [rsp+32], 0
    sti
    int 0xff
    cli
    jmp .restored
.sleep:
    sti
    hlt
    cli

; ====== restored_registers ======

.restored:
%macro checked 2
    cmp %1, %2
    jne .bad
%endmacro
    checked rax, 0x12345001
    checked rbx, 0x12345002
    checked rcx, 0x12345003
    checked rdx, 0x12345004
    checked rbp, 0x12345005
    checked rsi, 0x12345006
    checked rdi, 0x12345007
    checked r8, 0x12345008
    checked r9, 0x12345009
    checked r10, 0x1234500a
    checked r11, 0x1234500b
    checked r12, 0x1234500c
    checked r13, 0x1234500d
    checked r14, 0x1234500e
    checked r15, 0x1234500f
    pushfq
    pop rax
    test eax, 0x400
    jz .bad
    cld
    fxsave64 [rsp+64]
    lea rcx, [rsp+64]
    [warning -reloc-rel-dword]
    call xis_native_verified
    [warning +reloc-rel-dword]
%ifdef KERNEL_AP_ADMISSION
    [warning -reloc-rel-dword]
    call xad_native_poll
    [warning +reloc-rel-dword]
%endif
    jmp .pattern

; ====== unexpected_context_change ======

.bad:
    cli
    cld
    mov rcx, 0x4b34495049475052
    [warning -reloc-rel-dword]
    call xis_native_abort
    [warning +reloc-rel-dword]
    ud2

%else
%ifdef KERNEL_AP_ADMISSION
extern xad_native_poll
%endif

; ====== xis_service_loop ======

xis_service_loop:
    sub rsp, 576
.idle:
    sti
    hlt
    cli
%ifdef KERNEL_AP_ADMISSION
    cld
    [warning -reloc-rel-dword]
    call xad_native_poll
    [warning +reloc-rel-dword]
%endif
    jmp .idle
%endif

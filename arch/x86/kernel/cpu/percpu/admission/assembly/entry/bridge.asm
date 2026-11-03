; ====== private_c_bridge ======

; Microsoft x64 ABI, Calling convention defaults/Alignment: RCX/RDX,
; 32-byte shadow space; RSP 16-aligned before CALL, R15 nonvolatile.
; Intel SDM 253668-078US 6.7.1: no IRET before admission; existing resume
; performs IRETQ with the final IST IDT. CLI/HLT here are terminal only.
bits 64
default rel
%ifidn __OUTPUT_FORMAT__,win64
section .text
global xs_private_arrive, xs_private_park
extern xa_native_admit, xa_native_park, xs_private_resume
%ifdef KERNEL_AP_SERVICE
extern xis_native_enter, xis_service_loop
%endif
%endif

; ====== xs_private_arrive ======

xs_private_arrive:
    cmp rsp, r15
    jne xa_stop
    test rsp, 15
    jnz xa_stop
    cmp rcx, r15
    jne xa_stop
    cmp r8, r15
    jne xa_stop
    sub rsp, 32
    [warning -reloc-rel-dword]
    call xa_native_admit
    [warning +reloc-rel-dword]
    mov rsp, r15
    test eax, eax
    jnz xa_stop
    mov rcx, r15
    [warning -reloc-rel-dword]
    jmp xs_private_resume
    [warning +reloc-rel-dword]

; ====== xs_private_park ======

xs_private_park:
    cmp rsp, r15
    jne xa_stop
    test rsp, 15
    jnz xa_stop
    cmp rcx, r15
    jne xa_stop
    cmp r8, r15
    jne xa_stop
    sub rsp, 32
    [warning -reloc-rel-dword]
    call xa_native_park
    [warning +reloc-rel-dword]
    mov rsp, r15
%ifdef KERNEL_AP_SERVICE
    test eax, eax
    jnz xa_stop
    mov rcx, r15
    sub rsp, 32
    [warning -reloc-rel-dword]
    call xis_native_enter
    [warning +reloc-rel-dword]
    mov rsp, r15
    [warning -reloc-rel-dword]
    jmp xis_service_loop
    [warning +reloc-rel-dword]
%endif

; ====== xa_stop ======

xa_stop:
    cli
.halt:
    hlt
    jmp .halt

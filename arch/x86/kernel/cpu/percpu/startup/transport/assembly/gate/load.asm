; ====== aligned_mailbox_acquire ======

; Intel SDM 253668-078US 9.1.1: aligned DWORD loads/stores are atomic.
; 9.2.2: WB loads preserve acquire ordering; the ASM writer publishes Gate
; after IVT setup. Opaque C-call boundary; no write/RMW to the RO loan.
bits 64
section .text
global xn_gate_load
xn_gate_load:
    mov eax, [rcx]
    ret

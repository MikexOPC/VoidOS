; ====== mixed_mode_assembler_fixture ======

; NASM 3.01 manual, BITS directive and bin output: operand size selection.
; Intel SDM Vol. 2B, MOV--Move: B8+rw iw, B8+rd id, REX.W+B8+rd io.
; Offline assembler fixture only; no hardware registers or mode switch.
org 0
bits 16
mov ax, 0x1234
bits 32
mov eax, 0x12345678
bits 64
mov rax, 0x1122334455667788

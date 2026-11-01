; ====== diagnostic_thunk_to_real_park_bridge ======

org 0xffffffff80102000
bits 64
mov rax, 0xffffffff80100f00
jmp qword [rax]

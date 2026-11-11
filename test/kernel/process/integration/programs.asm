; ====== ELF dei programmi di prova nell'immagine del kernel ======

section .rdata rdata align=8

%macro PROGRAM 1
    %defstr program_name %1
    %strcat program_file "k6-", program_name, ".elf"
    global k6_program_%1
    global k6_program_%1_length
    align 8
k6_program_%1:
    incbin program_file
k6_program_%1_end:
    align 8
k6_program_%1_length:
    dq k6_program_%1_end - k6_program_%1
%endmacro

PROGRAM hello
PROGRAM null
PROGRAM readonly
PROGRAM privileged
PROGRAM kernel_jump
PROGRAM bad_pointer
PROGRAM bss
PROGRAM irq
PROGRAM spin
PROGRAM syscall_spin
PROGRAM flags_spin
PROGRAM flags_ac
PROGRAM write_flood
PROGRAM spurious
PROGRAM yield_a
PROGRAM yield_b

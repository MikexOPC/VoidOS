; ====== ELF del primo programma nell'immagine del kernel ======

section .rdata rdata align=8

global kernel_user_init
global kernel_user_init_length

kernel_user_init:
    incbin "user-init.elf"
kernel_user_init_end:
    align 8
kernel_user_init_length:
    dq kernel_user_init_end - kernel_user_init

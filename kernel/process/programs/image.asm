; ====== ELF del primo programma nell'immagine del kernel ======

section .rdata rdata align=8

global kernel_user_init
global kernel_user_init_end

kernel_user_init:
    incbin "user-init.elf"
kernel_user_init_end:

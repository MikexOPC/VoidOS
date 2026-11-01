; ====== link_only_continuation ======

bits 64
section .text
global xs_private_arrive
xs_private_arrive:
    cli
.halt:
    hlt
    jmp .halt

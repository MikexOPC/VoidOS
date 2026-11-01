; ====== diagnostic_destination_never_launched ======

; Intel SDM Vol.2A CLI/HLT instruction entries: clear IF[9], terminal CPL0 halt.
bits 64
section .text
global xs_installation_terminal
xs_installation_terminal:
    cli
.halt:
    hlt
    jmp .halt

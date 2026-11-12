; ====== ELF del bridge nell'immagine del kernel ======

section .rdata rdata align=8
global kernel_user_bridge
global kernel_user_bridge_length
kernel_user_bridge:
    incbin "user-bridge.elf"
kernel_user_bridge_end:
    align 8
kernel_user_bridge_length:
    dq kernel_user_bridge_end - kernel_user_bridge

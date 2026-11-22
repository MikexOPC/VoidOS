; ====== ELF del worker utente nell'immagine del kernel ======

section .rdata rdata align=8
global kernel_user_worker
global kernel_user_worker_length
kernel_user_worker:
    incbin "user-worker.elf"
kernel_user_worker_end:
    align 8
kernel_user_worker_length:
    dq kernel_user_worker_end - kernel_user_worker

; ====== static_link_callbacks_only ======

bits 64
section .text
global xa_native_admit, xa_native_park
xa_native_admit:
    mov eax, 1
    ret
xa_native_park:
    mov eax, 1
    ret

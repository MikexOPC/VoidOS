; ====== controlled_blob_fixture_loader ======

; Isolated SeaBIOS fixture. The owned host initializes all blob memory before releasing this gate.
; No NMI is injected before the blob publishes IVT readiness.
org 0x7c00
bits 16
%ifndef CODE_SEG
%error CODE_SEG required
%endif
cli
cld
xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7000
mov dword [0x5008], 0
mov dword [0x5000], 1
.wait:
pause
cmp dword [0x5008], 1
jne .wait
jmp CODE_SEG:0
times 510 - ($ - $$) db 0
dw 0xaa55

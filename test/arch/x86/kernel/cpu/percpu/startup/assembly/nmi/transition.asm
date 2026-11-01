; ====== isolated_nmi_transition_fixture ======

; Intel SDM 253668-078US 6.7/6.7.1: true NMI blocks later NMIs until IRET.
; 10.7.1: real IVT vector 2 is four bytes at base+8; 10.8.5.2: IDT mode gap.
; 4.1.2/4.5: CR4.PAE=5, EFER.LME=8, CR0.PG=31; CR0.WP=16, PE=0.
; Test-only SeaBIOS sector on one CPU; not the OS/AP trampoline or production paging.
org 0x7c00
bits 16
cli
xor ax, ax
mov ds, ax
mov ss, ax
mov sp, 0x7000
cld
mov di, 0x5000
mov cx, 8
rep stosw
mov eax, 1
cpuid
and edx, (1 << 5) | (1 << 6)
cmp edx, (1 << 5) | (1 << 6)
jne fail
mov eax, 0x80000000
cpuid
cmp eax, 0x80000001
jb fail
mov eax, 0x80000001
cpuid
test edx, 1 << 29
jz fail

; ====== tables_and_ivt ======

mov ax, 0x1000
mov es, ax
xor di, di
xor eax, eax
mov cx, 8192
rep stosw
mov dword [es:0], 0x11003
mov dword [es:0x1000], 0x12003
mov dword [es:0x2000], 0x13003
mov di, 0x3000
mov eax, 3
mov cx, 512
.leaf:
mov [es:di], eax
add eax, 4096
add di, 8
loop .leaf
lidt [ivtr - $$ + 0x7c00]
lgdt [gdtr - $$ + 0x7c00]
mov dword [0x5000], 1
.wait:
hlt
jmp .wait

; ====== real_nmi ======

real_nmi:
inc dword [0x5004]
mov eax, cr0
or eax, 1
mov cr0, eax
jmp dword 8:(protected - $$ + 0x7c00)

; ====== protected_nmi ======

bits 32
protected:
mov ax, 0x10
mov ds, ax
mov es, ax
mov ss, ax
mov esp, 0x7000
mov dword [0x5000], 2
.wait:
pause
cmp dword [0x5008], 1
jne .wait
mov eax, 0x20
mov cr4, eax
mov eax, 0x10000
mov cr3, eax
mov ecx, 0xc0000080
rdmsr
or eax, 0x100
wrmsr
mov eax, cr0
or eax, 0x80010000
mov cr0, eax
jmp 0x18:(long_mode - $$ + 0x7c00)

; ====== long_mode_nmi ======

bits 64
default abs
long_mode:
lidt [rel idtr64]
mov dword [0x5000], 3
.wait:
pause
cmp dword [0x500c], 1
jne .wait
push 0x10
push 0x7000
push 2
push 0x18
lea rax, [rel returned]
push rax
iretq
returned:
mov dword [0x5000], 5
.halt:
hlt
jmp .halt
final_nmi:
inc dword [0x5004]
iretq

; ====== fixture_descriptors ======

; Intel SDM 078US 3.4.5, 6.14.1: flat accessed code/data; 64-bit gate type E.
align 8
gdt:
dq 0, 0x00cf9b000000ffff, 0x00cf93000000ffff, 0x00af9b000000ffff
gdtr:
dw 31
dd gdt - $$ + 0x7c00
ivtr:
dw 11
dd ivt - $$ + 0x7c00
ivt:
dq 0
dw real_nmi - $$ + 0x7c00, 0
idt64:
times 32 db 0
dw final_nmi - $$ + 0x7c00, 0x18
db 0, 0x8e
dw 0
dd 0, 0
idtr64:
dw 47
dq idt64 - $$ + 0x7c00

; ====== unsupported_cpu ======

bits 16
fail:
mov dword [0x5000], 0xff
hlt
jmp fail
times 510 - ($ - $$) db 0
dw 0xaa55

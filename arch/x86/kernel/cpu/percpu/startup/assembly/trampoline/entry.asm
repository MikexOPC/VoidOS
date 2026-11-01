; ====== relocatable_ap_blob ======

; Intel SDM 253668-078US 6.7.1/10.8.5: NMI blocking survives until IRET.
; 4.1.2/4.5: CR4.PAE5, EFER.LME8/NXE11, CR0.PG31/WP16/PE0.
; 2.5: CR0.MP1/EM2/TS3/NE5, CR4.OSFXSR9/OSXMMEXCPT10 for the scalar/SSE2 profile.
; Vol.2A CPUID: 01H:EDX[5,6,16,24,25,26] MSR/PAE/PAT/FXSR/SSE/SSE2;
; 80000001H:EDX[29,20] long mode/NX. Unsupported profiles stop before transition.
; 12.12.2: IA32_PAT=277h; compare only, no cache-policy change here.
; Header/data offsets are software ABI, not hardware register offsets.
org 0
bits 16
jmp short entry16
dd 0x31504158
dw 1, blob_end - $$
dw 0
dd 0
dw entry32 - $$, entry64 - $$, gdt - $$, nmi16 - $$
dw fail16 - $$, fail32 - $$, fail64 - $$, 0

; ====== entry16 ======

entry16:
cli
cld
mov ax, cs
mov ds, ax
mov ax, [cs:10]
mov ss, ax
mov sp, 4096
mov es, ax
lidt [es:136]
cmp dword [es:0], 1
jne fail16
cmp dword [es:4], 104
jne fail16
mov eax, 1
cpuid
mov eax, edx
and eax, (1 << 5) | (1 << 6) | (1 << 16) | (1 << 24) | (1 << 25) | (1 << 26)
cmp eax, (1 << 5) | (1 << 6) | (1 << 16) | (1 << 24) | (1 << 25) | (1 << 26)
jne fail16
shr ebx, 24
cmp ebx, [es:12]
jne fail16
mov eax, 0x80000000
cpuid
cmp eax, 0x80000001
jb fail16
mov eax, 0x80000001
cpuid
and edx, (1 << 29) | (1 << 20)
cmp edx, (1 << 29) | (1 << 20)
jne fail16
mov dword [es:104], 1
.wait:
hlt
jmp .wait

; ====== fail16 ======

fail16:
cli
mov dword [es:108], 1
mov dword [es:104], -1
.halt:
hlt
jmp .halt

; ====== nmi16 ======

nmi16:
mov eax, 1
mov edx, 2
lock cmpxchg dword [es:104], edx
jne fail16
lgdt [es:112]
mov ebx, [cs:12]
mov eax, cr0
or eax, 1
mov cr0, eax
jmp dword far [es:120]

; ====== entry32 ======

bits 32
entry32:
mov ax, 0x10
mov ds, ax
mov es, ax
mov ss, ax
mov esi, ebx
lea esp, [esi + 4096]
lidt [esi + 144]
mov ecx, 0x277
rdmsr
cmp eax, [esi + 88]
jne fail32
cmp edx, [esi + 92]
jne fail32
mov eax, (1 << 5) | (1 << 9) | (1 << 10)
mov cr4, eax
mov eax, [esi + 40]
mov cr3, eax
mov ecx, 0xc0000080
rdmsr
or eax, (1 << 8) | (1 << 11)
wrmsr
mov eax, cr0
and eax, ~((1 << 2) | (1 << 3))
or eax, (1 << 31) | (1 << 16) | (1 << 5) | (1 << 1)
mov cr0, eax
jmp far [esi + 128]

; ====== fail32 ======

fail32:
cli
mov dword [esi + 108], 2
mov dword [esi + 104], -1
.halt:
hlt
jmp .halt

; ====== entry64 ======

bits 64
default rel
entry64:
mov esi, esi
mov esp, esp
lidt [rsi + 152]
mov rcx, rsi
mov rdx, [rsi + 16]
mov rax, [rsi + 80]
mov dword [rsi + 104], 3
jmp rax

; ====== fail64 ======

fail64:
cli
mov dword [rsi + 108], 3
mov dword [rsi + 104], -1
.halt:
hlt
jmp .halt

; ====== initial_gdt ======

; Intel SDM 078US 3.4.5: accessed descriptors avoid writes to the RO code page.
; Selectors 8/10h/18h: flat code32, data, code64 (L1/D0).
align 8
gdt:
dq 0, 0x00cf9b000000ffff, 0x00cf93000000ffff, 0x00af9b000000ffff
blob_end:
times 4096 - ($ - $$) db 0

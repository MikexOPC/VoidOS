; ====== high_entry_fixture ======

; Intel SDM 253668-078US 4.6: CR0.WP16 and EFER.NXE11 enforce supervisor RO/XD leaves.
; Test-only high assembly destination, not kernel C entry or AP readiness.
bits 64
default rel
entry:
cmp rcx, rsi
jne bad
cmp rdx, [rsi + 16]
jne bad
%if MODE == 1
jmp rsi
%elif MODE == 2
mov rax, [rsi + 24]
mov byte [rax], 0
%else
mov dword [rsi + 104], 4
%endif
.halt:
hlt
jmp .halt
bad:
mov dword [rsi + 108], 4
mov dword [rsi + 104], -1
.halt:
hlt
jmp .halt

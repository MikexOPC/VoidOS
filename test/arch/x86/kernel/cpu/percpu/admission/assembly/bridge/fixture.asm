; ====== actual_bridge_cpu_fixture ======

org 0xffffffff80100000
bits 64
default rel
    jmp strict near xs_private_entry
    [warning -reloc-abs-qword]
    dq xs_return_iret, final_nmi, final_fault
    [warning +reloc-abs-qword]
%include "arch/x86/kernel/cpu/percpu/startup/assembly/private/entry.asm"
%include "arch/x86/kernel/cpu/percpu/startup/assembly/private/resume.asm"
%include "arch/x86/kernel/cpu/percpu/admission/assembly/entry/bridge.asm"

; ====== admit_abi_model ======

xa_native_admit:
    cmp rcx, r15
    jne xs_private_fault
    cmp rdx, [r15 + 16]
    jne xs_private_fault
    lea rax, [rsp + 40]
    cmp rax, r15
    jne xs_private_fault
    mov rax, rsp
    and eax, 15
    cmp eax, 8
    jne xs_private_fault
    mov qword [rsp + 8], 0x1234
    mov qword [rsp + 32], 0x5678
    mov dword [r15 + 664], 3
    mov ecx, 100000000
.wait:
    cmp dword [gs:280], 1
    je .done
    pause
    loop .wait
    jmp xs_private_fault
.done:
    mov rcx, -1
    mov rdx, -1
    mov r8, -1
    mov r9, -1
    mov r10, -1
    mov r11, -1
    xor eax, eax
    ret

; ====== park_abi_model ======

xa_native_park:
    cmp rcx, r15
    jne xs_private_fault
    cmp rdx, [r15 + 16]
    jne xs_private_fault
    cmp r8, r15
    jne xs_private_fault
    lea rax, [rsp + 40]
    cmp rax, r15
    jne xs_private_fault
    mov rax, rsp
    and eax, 15
    cmp eax, 8
    jne xs_private_fault
    mov qword [rsp + 8], 0x3456
    mov qword [rsp + 32], 0x789a
    pushfq
    pop rax
    test eax, 0x7600
    jnz xs_private_fault
    mov rax, cr3
    cmp rax, [r15 + 48]
    jne xs_private_fault
    mov dword [r15 + 664], 4
    xor eax, eax
    ret

; ====== final_nmi_fixture ======

final_nmi:
    mov [gs:264], rsp
    inc qword [gs:256]
    iretq

; ====== final_fault_fixture ======

final_fault:
    cli
    mov [gs:288], rsp
    mov dword [r15 + 668], 2
    mov dword [r15 + 664], 0xfffffffe
.halt:
    hlt
    jmp .halt

; ====== diagnostic_return_thunk_address ======

times (0xf00 - ($ - $$)) db 0
    [warning -reloc-abs-qword]
    dq xs_private_park
    [warning +reloc-abs-qword]

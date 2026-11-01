; ====== shared_prefix_and_resume ======

org 0xffffffff80100000
bits 64
default rel
    jmp strict near xs_private_entry
    [warning -reloc-abs-qword]
    dq xs_return_iret, final_nmi, final_fault
    [warning +reloc-abs-qword]
%include "arch/x86/kernel/cpu/percpu/startup/assembly/private/entry.asm"
%include "arch/x86/kernel/cpu/percpu/startup/assembly/private/resume.asm"

; ====== fixture_wait_for_plan ======

xs_private_arrive:
    cmp rcx, r15
    jne xs_private_fault
    cmp rdx, [rcx + 16]
    jne xs_private_fault
    mov dword [r15 + 664], 3
    mov ecx, 100000000
.wait:
    cmp dword [gs:280], 1
    je .resume
    pause
    loop .wait
    jmp xs_private_fault
.resume:
    mov rcx, r15
    jmp xs_private_resume

; ====== final_nmi_fixture ======

; Intel SDM 078US 6.14.2/6.14.5: IST2 switch, hardware frame of five qwords.
final_nmi:
    mov [gs:264], rsp
    inc qword [gs:256]
    iretq

; ====== final_fault_fixture ======

; Terminal only: hardware exception error code/frame are not returned through.
final_fault:
    cli
    mov [gs:288], rsp
    mov dword [r15 + 668], 2
    mov dword [r15 + 664], 0xfffffffe
.halt:
    hlt
    jmp .halt

#ifndef OS_X86_SCHEDULER_CONTEXT_H
#define OS_X86_SCHEDULER_CONTEXT_H

#include <stdint.h>
#include <stddef.h>

#define X86_TASK_STACK_BYTES UINT64_C(65536)
#define X86_TASK_FX_BYTES 512U
#define X86_TASK_CONTEXT_BYTES 592U

typedef __declspec(align(16)) struct {
    uint64_t  rsp;
    uint64_t  rbx;
    uint64_t  rbp;
    uint64_t  rdi;
    uint64_t  rsi;
    uint64_t  r12;
    uint64_t  r13;
    uint64_t  r14;
    uint64_t  r15;
    uint64_t  padding;
    unsigned char fx[X86_TASK_FX_BYTES];
} X86_TASK_CONTEXT;

typedef struct {
    uint64_t  cr0;
    uint64_t  cr3;
    uint64_t  cr4;
    uint64_t  flags;
    uint64_t  apic_base;
    uint32_t  max_leaf;
    uint32_t  features;
    uint32_t  apic_id;
    uint32_t  cpl;
    uint64_t  reserved;
} X86_TASK_CPU;

_Static_assert(sizeof(X86_TASK_CONTEXT) == 592 && _Alignof(X86_TASK_CONTEXT) == 16,
               "Context size and alignment ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, rsp) == 0, "RSP ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, rbx) == 8, "RBX ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, rbp) == 16, "RBP ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, rdi) == 24, "RDI ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, rsi) == 32, "RSI ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, r12) == 40, "R12 ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, r13) == 48, "R13 ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, r14) == 56, "R14 ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, r15) == 64, "R15 ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, padding) == 72, "Padding ABI");
_Static_assert(offsetof(X86_TASK_CONTEXT, fx) == 80, "FX ABI");
_Static_assert(sizeof(X86_TASK_CPU) == 64, "CPU snapshot ABI");
_Static_assert(offsetof(X86_TASK_CPU, cr0) == 0, "CPU CR0 ABI");
_Static_assert(offsetof(X86_TASK_CPU, cr3) == 8, "CPU CR3 ABI");
_Static_assert(offsetof(X86_TASK_CPU, cr4) == 16, "CPU CR4 ABI");
_Static_assert(offsetof(X86_TASK_CPU, flags) == 24, "CPU flags ABI");
_Static_assert(offsetof(X86_TASK_CPU, apic_base) == 32, "CPU APIC ABI");
_Static_assert(offsetof(X86_TASK_CPU, max_leaf) == 40, "CPU max leaf ABI");
_Static_assert(offsetof(X86_TASK_CPU, features) == 44, "CPU features ABI");
_Static_assert(offsetof(X86_TASK_CPU, apic_id) == 48, "CPU APIC ID ABI");
_Static_assert(offsetof(X86_TASK_CPU, cpl) == 52, "CPU CPL ABI");
_Static_assert(offsetof(X86_TASK_CPU, reserved) == 56, "CPU reserved ABI");

// ====== scheduler_context_backend ======

// Private API: output/stack must be owned, stable, writable objects; no user pointers.
// Native caller must first pass cpu_valid and verify dispatcher/outside IRQ/NMI.
// Authority callback binds this exact context to an owned RW/NX guarded stack.
int x86_scheduler_context_prepare(X86_TASK_CONTEXT *output, uint64_t base, uint64_t bytes);
int x86_scheduler_context_resume_valid(const X86_TASK_CONTEXT *context, uint64_t base,
    uint64_t bytes, uint64_t code_begin, uint64_t code_end, uint32_t mxcsr_mask);
int x86_scheduler_cpu_valid(const X86_TASK_CPU *cpu, uint64_t expected_root, uint32_t bsp_id);
int x86_scheduler_cpu_capture(X86_TASK_CPU *output);
int x86_scheduler_fp_template(unsigned char *output);

// ====== scheduler_private_switch ======

// Raw Assembly, not a checked user API. Requires distinct canonical owned contexts,
// validated next image/return slot, live stack leases and no Rust borrow across switch.
// No unwind, TLS migration, IRQ preemption, CET, AVX/AMX or SMP contract.
void x86_scheduler_context_switch(X86_TASK_CONTEXT *previous, const X86_TASK_CONTEXT *next);
void x86_scheduler_task_bootstrap(void);
// Required integration symbols; host fixtures supply their own implementations.
int x86_scheduler_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes);
void x86_scheduler_task_start(void);

#endif

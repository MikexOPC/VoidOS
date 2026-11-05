#include "arch/x86/kernel/scheduler/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86_TASK_CONTEXT context;
static __declspec(align(4096)) struct {
    unsigned char low[4096], stack[X86_TASK_STACK_BYTES], high[4096];
} stack_storage;
#define stack stack_storage.stack
static uint32_t checks;
static int authority = 1;
extern uint64_t x86_scheduler_cpu_permissions(void);
extern int x86_scheduler_irq_compute(const volatile uint32_t *, uint32_t, uint32_t, uint32_t);

// ====== check_private ======

static void check_at_private(int value, unsigned int line)
{
    if (!value) { fprintf(stderr, "Context check failed %u at line %u\n", checks + 1, line); exit(1); }
    ++checks;
}

#define check_private(value) check_at_private((value), __LINE__)

// ====== x86_scheduler_stack_authority ======

int x86_scheduler_stack_authority(const X86_TASK_CONTEXT *owner, uint64_t base, uint64_t bytes)
{
    return owner == &context && base == (uint64_t)(uintptr_t)stack &&
           bytes == sizeof(stack) ? authority : 0;
}

// ====== x86_scheduler_task_start ======

void x86_scheduler_task_start(void) { abort(); }

// ====== cpu_policy_private ======

static void cpu_policy_private(void)
{
    X86_TASK_CPU valid = {0}, bad;
    uint32_t bit;
    valid.cr0 = (UINT64_C(1) << 31) | (UINT64_C(1) << 16) | 1;
    valid.cr3 = 0x200000;
    valid.cr4 = UINT64_C(1) << 9;
    valid.apic_base = UINT64_C(1) << 8;
    valid.max_leaf = 1;
    valid.features = (1U << 9) | (1U << 24) | (1U << 25) | (1U << 26);
    valid.apic_id = 2;
    check_private(x86_scheduler_cpu_valid(&valid, valid.cr3, 2));
    check_private(!x86_scheduler_ap_cpu_valid(&valid,valid.cr3,2));
    bad = valid; bad.apic_base &= ~(1ULL << 8);
    check_private(x86_scheduler_ap_cpu_valid(&bad,bad.cr3,2));
    check_private(!x86_scheduler_cpu_valid(&bad,bad.cr3,2));
    check_private(!x86_scheduler_ap_cpu_valid(&bad,bad.cr3,3));
    bad.flags |= 512;
    check_private(!x86_scheduler_ap_cpu_valid(&bad,bad.cr3,2));
    for (bit = 0; bit < 32; ++bit) {
        bad = valid;
        bad.features &= ~(1U << bit);
        check_private(x86_scheduler_cpu_valid(&bad, bad.cr3, 2) ==
                      ((valid.features & (1U << bit)) == 0));
    }
    bad = valid; bad.cr0 |= 4; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.cr0 |= 8; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.cr4 = 0; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.cr4 |= UINT64_C(1) << 12; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.cr4 |= UINT64_C(1) << 23; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.flags = UINT64_C(1) << 9; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.flags = UINT64_C(1) << 10; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.cpl = 3; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.apic_base = 0; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.reserved = 1; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.max_leaf = 0; check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    check_private(!x86_scheduler_cpu_valid(&valid, 0, 2));
    check_private(!x86_scheduler_cpu_valid(&valid, valid.cr3 + 4096, 2));
    check_private(!x86_scheduler_cpu_valid(&valid, valid.cr3, 3));
    check_private(!x86_scheduler_cpu_valid(&valid, valid.cr3, 256));
    bad = valid; bad.cr0 &= ~UINT64_C(1);
    check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    bad = valid; bad.cr3 = UINT64_C(1) << 52;
    check_private(!x86_scheduler_cpu_valid(&bad, bad.cr3, 2));
    memset(&bad, 0xA5, sizeof(bad));
    valid = bad;
    check_private(!x86_scheduler_cpu_capture(&bad));
    check_private(memcmp(&bad, &valid, sizeof(bad)) == 0); // Real ring3 denial before CR/MSR reads.
    check_private(x86_scheduler_cpu_permissions() == 0); // EFER denied before RDMSR in ring3.
    check_private(x86_scheduler_irq_compute((const volatile uint32_t *)(uintptr_t)1, 0, 0, 0) == 0);
}

// ====== context_state_private ======

static void context_state_private(void)
{
    uint64_t base = (uint64_t)(uintptr_t)stack;
    uint64_t boot = (uint64_t)(uintptr_t)x86_scheduler_task_bootstrap;
    X86_TASK_CONTEXT before;
    uint32_t index, mask;
    memset(&context, 0xA5, sizeof(context));
    memset(stack, 0x5A, sizeof(stack));
    before = context;
    authority = 0;
    check_private(!x86_scheduler_context_prepare(&context, base, sizeof(stack)));
    check_private(memcmp(&context, &before, sizeof(before)) == 0);
    check_private(stack[sizeof(stack) - 1] == 0x5A);
    authority = 2;
    check_private(!x86_scheduler_context_prepare(&context, base, sizeof(stack)));
    authority = 1;
    check_private(!x86_scheduler_context_prepare(&context, base + 1, sizeof(stack)));
    check_private(!x86_scheduler_context_prepare(&context, base, sizeof(stack) - 1));
    check_private(!x86_scheduler_context_prepare((X86_TASK_CONTEXT *)(uintptr_t)base, base, sizeof(stack)));
    check_private(!x86_scheduler_context_prepare(NULL, base, sizeof(stack)));
    check_private(x86_scheduler_context_prepare(&context, base, sizeof(stack)));
    check_private(context.rsp == base + sizeof(stack) - 40);
    check_private(*(const uint64_t *)(uintptr_t)context.rsp == boot);
    mask = (uint32_t)context.fx[28] | ((uint32_t)context.fx[29] << 8);
    check_private(x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    for (index = 32; index < 512; ++index) check_private(context.fx[index] == 0);
    before = context;
    context.fx[27] = 0x80;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before; context.padding = 1;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before; context.rsp = base + sizeof(stack) - 8;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot + 1, boot + 2, mask));
    context.rsp = base - 8;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before; context.rsp += 1;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before; context.fx[0] &= 0xFE;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before; context.fx[25] &= 0xFE;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before; context.fx[28] ^= 1;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    context = before;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, 0));
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot, mask));
    authority = 0;
    check_private(!x86_scheduler_context_resume_valid(&context, base, sizeof(stack), boot, boot + 1, mask));
    authority = 1;
    check_private(!x86_scheduler_context_prepare(&context, UINT64_C(1) << 47, sizeof(stack)));
}

// ====== main ======

int main(void)
{
    uint32_t repetition;
    for (repetition = 0; repetition < 100; ++repetition) {
        cpu_policy_private();
        context_state_private();
    }
    printf("PASS: %u context/CPU policy checks (privileged snapshots MOCK)\n", checks);
    return 0;
}

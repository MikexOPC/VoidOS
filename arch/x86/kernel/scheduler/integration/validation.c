#include "control.h"
#include "arch/x86/configs/layout.h"

// ====== x86_scheduler_resume_valid ======

int x86_scheduler_resume_valid(const X86_TASK_CONTEXT *context, uint32_t index, uint32_t machine_mask)
{
    uint32_t mask;
    uint64_t target;
    const unsigned char *fx;
    if (!context || index >= 8) return 0;
    fx = context->fx;
    mask = (uint32_t)fx[28] | ((uint32_t)fx[29] << 8) | ((uint32_t)fx[30] << 16) | ((uint32_t)fx[31] << 24);
    if (!mask) mask = 0xFFBF;
    if (mask != machine_mask || !x86_scheduler_context_resume_valid(context,
        x86_task_stack_base(index),65536,X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT,machine_mask)) return 0;
    target = *(const uint64_t *)(uintptr_t)context->rsp;
    return x86_scheduler_code_valid(target);
}

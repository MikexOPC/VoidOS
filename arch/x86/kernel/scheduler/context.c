#include "context.h"

// ====== context_bounds_private ======

static int context_bounds_private(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes)
{
    uint64_t address = (uint64_t)(uintptr_t)context;
    uint64_t limit = UINT64_C(1) << 47;
    if (!context || (address & 15) || address > limit - sizeof(*context) ||
        bytes != X86_TASK_STACK_BYTES || base < 4096 || (base & 4095) ||
        base > limit - bytes - 4096) return 0;
    return address + sizeof(*context) <= base - 4096 || address >= base + bytes + 4096;
}

// ====== fx_u32_private ======

static uint32_t fx_u32_private(const unsigned char *fx, uint32_t offset)
{
    return (uint32_t)fx[offset] | ((uint32_t)fx[offset + 1] << 8) |
           ((uint32_t)fx[offset + 2] << 16) | ((uint32_t)fx[offset + 3] << 24);
}

// ====== x86_scheduler_context_prepare ======

int x86_scheduler_context_prepare(X86_TASK_CONTEXT *output, uint64_t base, uint64_t bytes)
{
    X86_TASK_CONTEXT candidate = {0};
    unsigned char control[2];
    uint32_t mxcsr, mask, index;
    volatile uint64_t *frame;
    if (!context_bounds_private(output, base, bytes) ||
        x86_scheduler_stack_authority(output, base, bytes) != 1) return 0;
    if (!x86_scheduler_fp_template(candidate.fx)) return 0;
    control[0] = candidate.fx[0]; control[1] = candidate.fx[1];
    mxcsr = fx_u32_private(candidate.fx, 24);
    mask = fx_u32_private(candidate.fx, 28);
    // Intel SDM 093 Vol.1 §11.6.6: zero MXCSR_MASK -> 0000FFBFh.
    if (!mask) mask = 0xFFBFU;
    if (control[0] != 0x7F || control[1] != 3 || mxcsr != 0x1F80 ||
        (mask & 0xFFFF0000U) || (mxcsr & ~mask)) return 0;
    // FNINIT leaves register payloads behind despite empty tags: scrub all data.
    // Intel Vol.2A Table 3-48: FCW0, MXCSR24, MASK28, ST32..159, XMM160..415.
    for (index = 0; index < X86_TASK_FX_BYTES; ++index) candidate.fx[index] = 0;
    candidate.fx[0] = control[0]; candidate.fx[1] = control[1];
    for (index = 0; index < 4; ++index) {
        candidate.fx[24 + index] = (unsigned char)(mxcsr >> (8 * index));
        candidate.fx[28 + index] = (unsigned char)(mask >> (8 * index));
    }
    candidate.rsp = base + bytes - 40;
    frame = (volatile uint64_t *)(uintptr_t)candidate.rsp;
    frame[0] = (uint64_t)(uintptr_t)x86_scheduler_task_bootstrap;
    for (index = 1; index < 5; ++index) frame[index] = 0;
    *output = candidate;
    return 1;
}

// ====== x86_scheduler_context_resume_valid ======

int x86_scheduler_context_resume_valid(const X86_TASK_CONTEXT *context, uint64_t base,
    uint64_t bytes, uint64_t code_begin, uint64_t code_end, uint32_t mxcsr_mask)
{
    uint64_t target;
    uint32_t mxcsr, saved_mask;
    if (!context_bounds_private(context, base, bytes) || code_begin >= code_end ||
        code_end > (UINT64_C(1) << 47) || !mxcsr_mask || (mxcsr_mask & 0xFFFF0000U) ||
        context->padding || (context->rsp & 15) != 8 || context->rsp < base ||
        context->rsp > base + bytes - 40) return 0;
    if (x86_scheduler_stack_authority(context, base, bytes) != 1) return 0;
    mxcsr = fx_u32_private(context->fx, 24);
    saved_mask = fx_u32_private(context->fx, 28);
    if (!saved_mask) saved_mask = 0xFFBFU;
    // v0 keeps all x87/SSE exceptions masked; controls/rounding remain per-task.
    if (saved_mask != mxcsr_mask || (mxcsr & ~mxcsr_mask) || (mxcsr & 0x1F80) != 0x1F80 ||
        (context->fx[0] & 63) != 63) return 0;
    target = *(const volatile uint64_t *)(uintptr_t)context->rsp;
    return target >= code_begin && target < code_end;
}

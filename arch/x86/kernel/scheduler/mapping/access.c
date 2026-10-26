#include "native.h"
#include "arch/x86/configs/layout.h"

// ====== x86_scheduler_native_bytes ======

volatile unsigned char *x86_scheduler_native_bytes(uint32_t index)
{
    uint64_t base = x86_task_stack_base(index);
    return base ? (volatile unsigned char *)(uintptr_t)base : 0;
}

// ====== x86_scheduler_code_entries ======

// Intel SDM093 Vol3A §5.5.4: recursive address, not a speculative page walk.
// Caller checks each parent before dereferencing the next level. Four-level only.
volatile uint64_t *x86_scheduler_code_entries(uint32_t level, uint64_t target)
{
    uint64_t r = X86_BOOT_RECURSIVE_SLOT, a, b, c, address;
    if (!X86_LAYOUT_RANGE(target, 1)) return 0;
    a = (target >> 39) & 511; b = (target >> 30) & 511; c = (target >> 21) & 511;
    if (level == 0) address = X86_LAYOUT_RECURSIVE_VIEW(r, r, r, r);
    else if (level == 1) address = X86_LAYOUT_RECURSIVE_VIEW(r, r, r, a);
    else if (level == 2) address = X86_LAYOUT_RECURSIVE_VIEW(r, r, a, b);
    else if (level == 3) address = X86_LAYOUT_RECURSIVE_VIEW(r, a, b, c);
    else return 0;
    return (volatile uint64_t *)(uintptr_t)address;
}

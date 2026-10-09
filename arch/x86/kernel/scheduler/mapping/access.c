#include "native.h"

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
    if (target >= (UINT64_C(1) << 47)) return 0;
    a = (target >> 39) & 511; b = (target >> 30) & 511; c = (target >> 21) & 511;
    if (level == 0) address = (r << 39) | (r << 30) | (r << 21) | (r << 12);
    else if (level == 1) address = (r << 39) | (r << 30) | (r << 21) | (a << 12);
    else if (level == 2) address = (r << 39) | (r << 30) | (a << 21) | (b << 12);
    else if (level == 3) address = (r << 39) | (a << 30) | (b << 21) | (c << 12);
    else return 0;
    return (volatile uint64_t *)(uintptr_t)address;
}

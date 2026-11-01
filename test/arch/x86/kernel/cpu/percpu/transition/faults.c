#include "faults.h"
#include <stdio.h>
#include <stdlib.h>
unsigned int xt_fail_alloc, xt_fail_pin, xt_fail_release, xt_stop_armed;
unsigned int xt_release_calls;
unsigned int xt_is_bsp = 1;
static unsigned int allocations, pins;
jmp_buf xt_terminal;
char xt_reason[80];

// ====== xt_fault_reset ======

void xt_fault_reset(void) {
    allocations = pins = xt_fail_alloc = xt_fail_pin = xt_fail_release = xt_stop_armed = 0;
    xt_release_calls = 0;
    xt_is_bsp = 1;
    xt_reason[0] = 0;
}

// ====== ks_boot_cpu ======

uint32_t ks_boot_cpu(void) { return xt_is_bsp; }

// ====== xt_test_allocate_within ======

int xt_test_allocate_within(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 pages, BOOT_U64 alignment,
    BOOT_U64 minimum, BOOT_U64 limit, KERNEL_PHYSICAL_LEASE *output) {
    if (++allocations == xt_fail_alloc) return 0;
    return kernel_physical_allocate_within(pool, pages, alignment, minimum, limit, output);
}

// ====== xt_test_pin ======

int xt_test_pin(KERNEL_PHYSICAL_ALLOCATOR *pool, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin) {
    if (pin && ++pins == xt_fail_pin) return 0;
    return kernel_physical_pin(pool, lease, pin);
}

// ====== xt_test_release_after_unmap ======

int xt_test_release_after_unmap(KERNEL_PHYSICAL_ALLOCATOR *pool, KERNEL_PHYSICAL_LEASE *lease) {
    if (++xt_release_calls == xt_fail_release) return 0;
    return kernel_physical_release_after_unmap(pool, lease);
}

// ====== xt_test_stop ======

__declspec(noreturn) void xt_test_stop(const char *reason) {
    for (unsigned int i = 0; i < sizeof(xt_reason) - 1; ++i) {
        xt_reason[i] = reason[i];
        if (!reason[i]) break;
    }
    xt_reason[sizeof(xt_reason) - 1] = 0;
    if (xt_stop_armed) longjmp(xt_terminal, 1);
    fprintf(stderr, "Unexpected transition stop: %s\n", xt_reason);
    abort();
}

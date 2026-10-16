#include "../watchdog.h"
#include "lib/memory/address_ranges.h"

static KERNEL_PHYSICAL_ALLOCATOR *loan_pool;
static KERNEL_PHYSICAL_LEASE loans[4];
static uint32_t loan_count;

// ====== x86_watchdog_budget_active ======

int x86_watchdog_budget_active(void) { return loan_pool != NULL; }

// ====== x86_watchdog_budget_valid ======

int x86_watchdog_budget_valid(void)
{
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    const KERNEL_PHYSICAL_ALLOCATOR *pool;
    if (!owner->Env.Window || !(pool = owner->Env.Window->Pool)) return 0;
    if (!loan_pool) return pool->FreePages == owner->Budget;
    if (loan_count < 2 || loan_count > 4 || pool != loan_pool ||
        owner->Budget < 16 * loan_count || pool->FreePages != owner->Budget - 16 * loan_count ||
        !kernel_physical_validate(pool)) return 0;
    for (uint32_t index = 0; index < loan_count; ++index)
        if (kernel_physical_is_pinned(pool,&loans[index]) != 1) return 0;
    return 1;
}

// ====== clear_private ======

static void clear_private(void)
{
    KERNEL_PHYSICAL_LEASE empty = {0};
    loan_pool = NULL; loan_count = 0;
    for (uint32_t index = 0; index < 4; ++index) loans[index] = empty;
}

// ====== x86_watchdog_budget_begin ======

int x86_watchdog_budget_begin(const KERNEL_PHYSICAL_LEASE *first, const KERNEL_PHYSICAL_LEASE *second)
{
    return x86_watchdog_budget_begin_three(first,second,NULL);
}

// ====== x86_watchdog_budget_begin_three ======

int x86_watchdog_budget_begin_three(const KERNEL_PHYSICAL_LEASE *first, const KERNEL_PHYSICAL_LEASE *second,
    const KERNEL_PHYSICAL_LEASE *third)
{
    return x86_watchdog_budget_begin_four(first,second,third,NULL);
}

// ====== x86_watchdog_budget_begin_four ======

int x86_watchdog_budget_begin_four(const KERNEL_PHYSICAL_LEASE *first, const KERNEL_PHYSICAL_LEASE *second,
    const KERNEL_PHYSICAL_LEASE *third, const KERNEL_PHYSICAL_LEASE *fourth)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    KERNEL_PHYSICAL_ALLOCATOR *pool;
    const KERNEL_PHYSICAL_LEASE *inputs[4] = {first,second,third,fourth};
    uint32_t count = fourth ? 4U : third ? 3U : 2U;
    if (loan_pool || !first || !second || first == second || !owner->Ready || owner->Closed ||
        owner->Armed || owner->Error || !owner->Env.Window || !(pool = owner->Env.Window->Pool) ||
        (fourth && !third) || owner->Budget < 16 * count ||
        pool->FreePages != owner->Budget - 16 * count || !kernel_physical_validate(pool)) return 0;
    for (uint32_t index = 0; index < count; ++index) {
        if (inputs[index]->Pages != 16 || kernel_physical_is_pinned(pool,inputs[index]) != 1) return 0;
        for (uint32_t other = 0; other < index; ++other)
            if (inputs[index] == inputs[other] || inputs[index]->Id == inputs[other]->Id ||
                os_address_ranges_overlap(inputs[index]->Base,65536,inputs[other]->Base,65536)) return 0;
    }
    for (uint32_t index = 0; index < count; ++index) loans[index] = *inputs[index];
    loan_count = count;
    loan_pool = pool;
    if (x86_watchdog_valid()) return 1;
    clear_private(); // Rejection rolls back accounting only; never free either lease.
    return 0;
}

// ====== x86_watchdog_budget_end ======

int x86_watchdog_budget_end(void)
{
    const X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    if (!loan_pool || owner->Armed || owner->Error || !x86_watchdog_valid()) return 0;
    // Borrow ends before task collect. The strict budget gate rejects further
    // device operations until the caller restores all scoped allocations to the pool.
    clear_private();
    return 1;
}

#include "../watchdog.h"
#include "lib/memory/address_ranges.h"

static KERNEL_PHYSICAL_ALLOCATOR *loan_pool;
static KERNEL_PHYSICAL_LEASE loans[3];
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
    if (loan_count < 2 || loan_count > 3) return 0;
    return pool == loan_pool && owner->Budget >= 16 * loan_count && pool->FreePages == owner->Budget - 16 * loan_count &&
        kernel_physical_validate(pool) && kernel_physical_is_pinned(pool,&loans[0]) == 1 &&
        kernel_physical_is_pinned(pool,&loans[1]) == 1 &&
        (loan_count == 2 || kernel_physical_is_pinned(pool,&loans[2]) == 1);
}

// ====== clear_private ======

static void clear_private(void)
{
    KERNEL_PHYSICAL_LEASE empty = {0};
    loan_pool = NULL; loan_count = 0; loans[0] = empty; loans[1] = empty; loans[2] = empty;
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
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    KERNEL_PHYSICAL_ALLOCATOR *pool;
    if (loan_pool || !first || !second || first == second || !owner->Ready || owner->Closed ||
        owner->Armed || owner->Error || !owner->Env.Window || !(pool = owner->Env.Window->Pool) ||
        owner->Budget < (third ? 48U : 32U) || pool->FreePages != owner->Budget - (third ? 48U : 32U) ||
        first->Pages != 16 || second->Pages != 16 || first->Id == second->Id ||
        !kernel_physical_validate(pool) || kernel_physical_is_pinned(pool,first) != 1 ||
        kernel_physical_is_pinned(pool,second) != 1 ||
        os_address_ranges_overlap(first->Base,65536,second->Base,65536)) return 0;
    if (third && (third == first || third == second || third->Pages != 16 ||
        third->Id == first->Id || third->Id == second->Id || kernel_physical_is_pinned(pool,third) != 1 ||
        os_address_ranges_overlap(third->Base,65536,first->Base,65536) ||
        os_address_ranges_overlap(third->Base,65536,second->Base,65536))) return 0;
    // Copies remain bounded; allocator identity/generation and pins stay authoritative.
    loans[0] = *first; loans[1] = *second; loan_count = third ? 3U : 2U;
    if (third) loans[2] = *third;
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

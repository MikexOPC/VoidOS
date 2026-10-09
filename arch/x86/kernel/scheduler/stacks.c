#include "stacks.h"

// ====== x86_task_stack_base ======

uint64_t x86_task_stack_base(uint32_t index)
{
    if (index >= X86_TASK_STACK_SLOTS) return 0;
    return X86_TASK_STACK_GUARD + ((uint64_t)index * X86_TASK_STACK_STRIDE + 1) * 4096;
}

// ====== empty_private ======

static int empty_private(const X86_TASK_STACK *owner)
{
    const unsigned char *bytes = (const unsigned char *)owner;
    size_t index;
    if (!owner) return 0;
    for (index = 0; index < sizeof(*owner); ++index) if (bytes[index]) return 0;
    return 1;
}

// ====== identity_private ======

static int identity_private(const X86_TASK_STACK *owner)
{
    uint64_t address, end, guard;
    if (!owner || owner->self != (uint64_t)(uintptr_t)owner || !owner->pool ||
        !owner->root || (owner->root & 4095) || !owner->generation ||
        owner->index >= X86_TASK_STACK_SLOTS || owner->stage < X86_TASK_STACK_LEASED ||
        owner->stage > X86_TASK_STACK_READY) return 0;
    guard = X86_TASK_STACK_GUARD;
    end = guard + (uint64_t)X86_TASK_STACK_SLOTS * X86_TASK_STACK_STRIDE * 4096;
    address = (uint64_t)(uintptr_t)owner;
    if (address > (UINT64_C(1) << 47) - sizeof(*owner) ||
        !(address + sizeof(*owner) <= guard || address >= end)) return 0;
    address = (uint64_t)(uintptr_t)owner->pool;
    if (address > (UINT64_C(1) << 47) - sizeof(*owner->pool) ||
        !(address + sizeof(*owner->pool) <= guard || address >= end)) return 0;
    return kernel_physical_validate(owner->pool) == 1;
}

// ====== lease_private ======

static int lease_private(const X86_TASK_STACK *owner)
{
    uint32_t index, matches = 0, bits = owner->pool->PhysicalBits;
    const KERNEL_PHYSICAL_LEASE *lease = &owner->lease;
    if (bits < 32 || bits > 52 || !lease->Id || lease->Owner != owner->pool->Self ||
        !lease->Base || (lease->Base & 4095) || lease->Pages != X86_TASK_STACK_PAGES ||
        lease->Base > (UINT64_C(1) << bits) - X86_TASK_STACK_BYTES) return 0;
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->pool->Live[index];
        if (live->Id == lease->Id && live->Owner == lease->Owner &&
            live->Base == lease->Base && live->Pages == lease->Pages) ++matches;
    }
    return matches == 1;
}

// ====== x86_task_stack_validate ======

int x86_task_stack_validate(const X86_TASK_STACK *owner)
{
    return identity_private(owner) && owner->stage == X86_TASK_STACK_READY &&
        lease_private(owner) && kernel_physical_is_pinned(owner->pool, &owner->lease) == 1 &&
        x86_task_stack_dispatcher(owner) == 1 && x86_task_stack_mapping_valid(owner, 1) == 1;
}

// ====== x86_task_stack_release ======

int x86_task_stack_release(X86_TASK_STACK *owner, uint32_t index, uint64_t generation)
{
    X86_TASK_STACK cleared = {0};
    int pinned;
    if (!identity_private(owner) || owner->index != index || owner->generation != generation ||
        !lease_private(owner) || x86_task_stack_dispatcher(owner) != 1) return 0;
    if (owner->stage >= X86_TASK_STACK_MAPPING) {
        if (x86_task_stack_revoke(owner) != 1 || x86_task_stack_mapping_valid(owner, 0) != 1) return 0;
        owner->stage = X86_TASK_STACK_LEASED;
    }
    if (x86_task_stack_mapping_valid(owner, 0) != 1) return 0;
    pinned = kernel_physical_is_pinned(owner->pool, &owner->lease);
    if (pinned != 0 && pinned != 1) return 0;
    if (pinned && (kernel_physical_pin(owner->pool, &owner->lease, 0) != 1 ||
                  kernel_physical_is_pinned(owner->pool, &owner->lease) != 0)) return 0;
    if (kernel_physical_release(owner->pool, &owner->lease) != 1) return 0;
    *owner = cleared;
    return 1;
}

// ====== x86_task_stack_acquire ======

int x86_task_stack_acquire(X86_TASK_STACK *owner, KERNEL_PHYSICAL_ALLOCATOR *pool,
                          uint64_t root, uint32_t index, uint64_t generation)
{
    X86_TASK_STACK candidate = {0};
    uint64_t address = (uint64_t)(uintptr_t)owner, pool_address = (uint64_t)(uintptr_t)pool;
    if (!owner || !pool || !empty_private(owner) || index >= X86_TASK_STACK_SLOTS || !generation ||
        (address < pool_address + sizeof(*pool) && pool_address < address + sizeof(*owner))) return 0;
    candidate.self = address; candidate.pool = pool; candidate.root = root;
    candidate.index = index; candidate.generation = generation; candidate.stage = X86_TASK_STACK_LEASED;
    // Validate original stable storage, not the local candidate address.
    *owner = candidate;
    if (!identity_private(owner) || x86_task_stack_dispatcher(owner) != 1 ||
        x86_task_stack_mapping_valid(owner, 0) != 1) {
        candidate = (X86_TASK_STACK){0}; *owner = candidate; return 0;
    }
    if (kernel_physical_allocate(pool, X86_TASK_STACK_PAGES, &owner->lease) != 1) {
        // Allocation failure must leave the initially empty output lease untouched.
        if (!owner->lease.Id && !owner->lease.Base && !owner->lease.Pages && !owner->lease.Owner) {
            candidate = (X86_TASK_STACK){0}; *owner = candidate;
        }
        return 0;
    }
    if (!lease_private(owner)) return 0; // Retain suspicious ownership, never free guessed frames.
    if (kernel_physical_pin(pool, &owner->lease, 1) != 1 ||
        kernel_physical_is_pinned(pool, &owner->lease) != 1) {
        (void)x86_task_stack_release(owner, index, generation); return 0;
    }
    owner->stage = X86_TASK_STACK_MAPPING;
    if (x86_task_stack_map(owner) != 1 || x86_task_stack_mapping_valid(owner, 1) != 1 ||
        x86_task_stack_zero(owner) != 1 || x86_task_stack_mapping_valid(owner, 1) != 1) {
        (void)x86_task_stack_release(owner, index, generation); return 0;
    }
    owner->stage = X86_TASK_STACK_READY;
    return x86_task_stack_validate(owner);
}

#include "arch/x86/kernel/scheduler/stacks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_TASK_STACK owners[8];
static uint32_t present[8], checks, failure, active, release_calls, revoke_calls;
static uint64_t next_id;

// ====== check_private ======

static void check_at_private(int value, unsigned int line)
{
    if (!value) { fprintf(stderr, "Resource check %u failed at %u\n", checks + 1, line); exit(1); }
    ++checks;
}
#define check_private(value) check_at_private((value), __LINE__)

// ====== physical_mock_identity ======

int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner)
{
    return owner == &pool && pool.Self == (uint64_t)(uintptr_t)&pool && pool.Ready == 1;
}

// ====== physical_mock_allocate ======

int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
                             KERNEL_PHYSICAL_LEASE *output)
{
    uint32_t id;
    check_private(owner == &pool && pages == 16);
    if (failure == 1) return 0;
    for (id = 0; id < 8; ++id) if (!pool.Live[id].Id) break;
    if (id == 8) return 0;
    output->Base = UINT64_C(0x100000) + id * X86_TASK_STACK_BYTES;
    output->Pages = pages; output->Id = ++next_id; output->Owner = pool.Self;
    pool.Live[id] = *output; pool.FreePages -= pages;
    return 1;
}

// ====== physical_mock_pin ======

int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin)
{
    uint32_t id;
    check_private(owner == &pool);
    if ((pin && failure == 2) || (!pin && (failure == 7 || failure == 10))) return 0;
    for (id = 0; id < 8; ++id) if (pool.Live[id].Id == lease->Id && lease->Id) {
        pool.Pins[id] = pin; return 1;
    }
    return 0;
}

// ====== physical_mock_pin_state ======

int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease)
{
    uint32_t id;
    check_private(owner == &pool);
    for (id = 0; id < 8; ++id) if (pool.Live[id].Id == lease->Id && lease->Id) return (int)pool.Pins[id];
    return -1;
}

// ====== physical_mock_release ======

int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease)
{
    uint32_t id;
    ++release_calls;
    check_private(owner == &pool);
    if (failure == 8 || failure == 11) return 0;
    for (id = 0; id < 8; ++id) if (pool.Live[id].Id == lease->Id && lease->Id) {
        check_private(!pool.Pins[id]);
        // Must revoke BEFORE unpin/free. Physical slot and task index may differ.
        for (uint32_t slot = 0; slot < 8; ++slot)
            if (owners[slot].lease.Id == lease->Id) check_private(present[slot] == 0);
        pool.FreePages += lease->Pages;
        memset(&pool.Live[id], 0, sizeof(pool.Live[id]));
        memset(lease, 0, sizeof(*lease)); return 1;
    }
    return 0;
}

// ====== mapping_mock_dispatcher ======

int x86_task_stack_dispatcher(const X86_TASK_STACK *owner)
{
    return !active && owner == &owners[owner->index] && owner->root == 0x200000;
}

// ====== mapping_mock_readback ======

int x86_task_stack_mapping_valid(const X86_TASK_STACK *owner, uint32_t mode)
{
    return present[owner->index] == (mode ? 16U : 0U);
}

// ====== mapping_mock_publish ======

int x86_task_stack_map(const X86_TASK_STACK *owner)
{
    check_private(kernel_physical_is_pinned(&pool, &owner->lease) == 1);
    check_private(!present[owner->index]);
    if (failure == 3) return 0;
    present[owner->index] = failure == 4 || failure == 9 ? 5 : 16;
    return failure != 4 && failure != 9;
}

// ====== mapping_mock_revoke ======

int x86_task_stack_revoke(const X86_TASK_STACK *owner)
{
    ++revoke_calls;
    if (failure == 6 || failure == 9) return 0;
    present[owner->index] = 0;
    return 1;
}

// ====== mapping_mock_zero ======

int x86_task_stack_zero(const X86_TASK_STACK *owner)
{
    check_private(present[owner->index] == 16);
    return failure != 5 && failure != 10 && failure != 11;
}

// ====== reset_private ======

static void reset_private(void)
{
    memset(&pool, 0, sizeof(pool)); memset(owners, 0, sizeof(owners));
    memset(present, 0, sizeof(present));
    pool.Self = (uint64_t)(uintptr_t)&pool; pool.Ready = 1; pool.PhysicalBits = 48;
    pool.FreePages = 1024;
    failure = active = release_calls = revoke_calls = 0;
}

// ====== lifecycle_private ======

static void lifecycle_private(void)
{
    uint32_t slot;
    X86_TASK_STACK copy, before;
    for (slot = 0; slot < 8; ++slot) {
        check_private(x86_task_stack_base(slot) == X86_TASK_STACK_GUARD + (slot * 18ULL + 1) * 4096);
        check_private(x86_task_stack_acquire(&owners[slot], &pool, 0x200000, slot, 1));
        check_private(x86_task_stack_validate(&owners[slot]));
    }
    check_private(pool.FreePages == 1024 - 128);
    check_private(!x86_task_stack_base(8));
    before = owners[0]; copy = before;
    check_private(!x86_task_stack_validate(&copy));
    check_private(!x86_task_stack_release(&owners[0], 0, 2));
    check_private(!x86_task_stack_release(&owners[0], 1, 1));
    check_private(memcmp(&before, &owners[0], sizeof(before)) == 0);
    active = 1;
    check_private(!x86_task_stack_release(&owners[0], 0, 1));
    check_private(release_calls == 0 && revoke_calls == 0);
    active = 0;
    for (slot = 0; slot < 8; ++slot) check_private(x86_task_stack_release(&owners[slot], slot, 1));
    check_private(pool.FreePages == 1024 && release_calls == 8);
}

// ====== failures_private ======

static void failures_private(void)
{
    uint32_t scenario;
    for (scenario = 1; scenario <= 5; ++scenario) {
        X86_TASK_STACK empty = {0};
        reset_private(); failure = scenario;
        check_private(!x86_task_stack_acquire(&owners[0], &pool, 0x200000, 0, 1));
        check_private(pool.FreePages == 1024 && !present[0]);
        check_private(memcmp(&owners[0], &empty, sizeof(empty)) == 0);
    }
    for (scenario = 6; scenario <= 8; ++scenario) {
        reset_private();
        check_private(x86_task_stack_acquire(&owners[0], &pool, 0x200000, 0, 1));
        failure = scenario;
        check_private(!x86_task_stack_release(&owners[0], 0, 1));
        check_private(owners[0].self && owners[0].lease.Id && pool.FreePages == 1008);
        if (scenario == 6) check_private(present[0] == 16 && release_calls == 0);
        if (scenario == 7) check_private(!present[0] && release_calls == 0);
        if (scenario == 8) check_private(!present[0]);
        failure = 0;
        check_private(x86_task_stack_release(&owners[0], 0, 1));
        check_private(pool.FreePages == 1024);
    }
    for (scenario = 9; scenario <= 11; ++scenario) {
        reset_private(); failure = scenario;
        check_private(!x86_task_stack_acquire(&owners[0], &pool, 0x200000, 0, 1));
        check_private(owners[0].self && owners[0].lease.Id && pool.FreePages == 1008);
        if (scenario == 9) check_private(present[0] == 5 && release_calls == 0);
        if (scenario == 10) check_private(!present[0] && release_calls == 0);
        if (scenario == 11) check_private(!present[0] && release_calls == 1);
        failure = 0;
        check_private(x86_task_stack_release(&owners[0], 0, 1));
        check_private(pool.FreePages == 1024);
    }
    reset_private();
    check_private(!x86_task_stack_acquire(&owners[0], &pool, 0x200000, 8, 1));
    check_private(!x86_task_stack_acquire(&owners[0], &pool, 0x200001, 0, 1));
    check_private(!x86_task_stack_acquire(&owners[0], &pool, 0x200000, 0, 0));
    check_private(x86_task_stack_acquire(&owners[0], &pool, 0x200000, 0, 1));
    ++owners[0].lease.Id;
    check_private(!x86_task_stack_release(&owners[0], 0, 1) && !release_calls);
    --owners[0].lease.Id;
    check_private(x86_task_stack_release(&owners[0], 0, 1));
}

// ====== main ======

int main(void)
{
    uint32_t iteration;
    for (iteration = 0; iteration < 1000; ++iteration) {
        reset_private(); lifecycle_private(); failures_private();
    }
    printf("PASS: %u resource checks, 8-slot lifecycle, rollback/retry/accounting (allocator/PTE MOCK)\n", checks);
    return 0;
}

#include "kernel/mm/physical/internal.h"
#include "kernel/sync/internal.h"
#include <string.h>
#include <stdio.h>
static KERNEL_PHYSICAL_ALLOCATOR pool, full;
static unsigned char map[48];
void physical_test_context_allowed(int allowed);
#define CHECK(test) do { if (!(test)) { printf("Physical query failure: %u\n", (unsigned int)__LINE__); return 1; } } while (0)

// ====== fixture_private ======

static int fixture_private(void) {
    memset(&pool, 0, sizeof(pool));
    memset(map, 0, sizeof(map));
    map[0] = 7; map[9] = 0x10; map[24] = 128; map[32] = 8;
    return kernel_physical_initialize(&pool, map, sizeof(map), sizeof(map), 32);
}

// ====== queries_private ======

static int queries_private(void) {
    KERNEL_PHYSICAL_STATUS status = {0}, before;
    KERNEL_PHYSICAL_LEASE lease = {0}, stale;
    BOOT_U64 budget = ~0ULL;
    CHECK(fixture_private());
    CHECK(kernel_physical_status(&pool, &status));
    CHECK(status.TotalPages == 128 && status.FreePages == 128 && !status.LivePages && !status.PinnedPages);
    CHECK(status.Owner == (BOOT_U64)&pool && !status.Generation);
    CHECK(kernel_physical_generation_read(&pool, &budget) && budget == 0);
    CHECK(!kernel_physical_generation_read(&pool, &pool.FreePages));
    CHECK(kernel_physical_budget_read(&pool, &budget) && budget == 128);
    CHECK(kernel_physical_budget_matches(&pool, 128) && !kernel_physical_budget_matches(&pool, 127));
    CHECK(kernel_physical_budget_at_least(&pool, 128) && !kernel_physical_budget_at_least(&pool, 129));
    CHECK(!kernel_physical_budget_read(&pool, &pool.FreePages));
    CHECK(kernel_physical_allocate(&pool, 2, &lease));
    CHECK(kernel_physical_pin(&pool, &lease, 1));
    CHECK(kernel_physical_is_pinned(&pool, &pool.Live[0]));
    CHECK(kernel_physical_lease_state(&pool, &lease, 1) && !kernel_physical_lease_state(&pool, &lease, 0));
    CHECK(!kernel_physical_lease_state(&pool, &lease, 2));
    CHECK(kernel_physical_range_pinned(&pool, lease.Base, lease.Pages));
    CHECK(!kernel_physical_range_pinned(&pool, lease.Base, lease.Pages + 1));
    CHECK(kernel_physical_status(&pool, &status));
    CHECK(status.FreePages == 126 && status.LivePages == 2 && status.PinnedPages == 2 && status.Generation == 2);
    CHECK(kernel_physical_generation_read(&pool, &budget) && budget == 2);
    CHECK(!kernel_physical_range_unowned(&pool, lease.Base, lease.Pages));
    CHECK(!kernel_physical_range_unowned(&pool, 0x8000, 1));
    CHECK(kernel_physical_range_unowned(&pool, 0x400000, 1));
    CHECK(!kernel_physical_range_unowned(&pool, 4097, 1));
    CHECK(!kernel_physical_range_unowned(&pool, 4096, 0));
    CHECK(!kernel_physical_range_unowned(&pool, 4096, ~0ULL));
    CHECK(kernel_physical_release_fits(&pool, &lease));
    stale = lease;
    CHECK(!kernel_physical_release(&pool, &lease));
    CHECK(kernel_physical_release_after_unmap(&pool, &lease) && !lease.Id);
    CHECK(!kernel_physical_release_after_unmap(&pool, &stale));
    CHECK(!kernel_physical_release_fits(&pool, &stale));
    CHECK(!kernel_physical_lease_state(&pool, &stale, 0) && !kernel_physical_lease_state(&pool, &stale, 1));
    CHECK(!kernel_physical_range_pinned(&pool, stale.Base, stale.Pages));
    CHECK(kernel_physical_status(&pool, &status));
    CHECK(status.FreePages == 128 && !status.LivePages && !status.PinnedPages && status.Generation == 4);
    before = status;
    CHECK(!kernel_physical_status(&pool, NULL));
    CHECK(!kernel_physical_status(&pool, (KERNEL_PHYSICAL_STATUS *)&pool));
    CHECK(!kernel_physical_status(&pool, (KERNEL_PHYSICAL_STATUS *)(uintptr_t)1));
    CHECK(!kernel_physical_status(&pool, (KERNEL_PHYSICAL_STATUS *)(uintptr_t)(UINTPTR_MAX - 7)));
    physical_test_context_allowed(0);
    CHECK(!kernel_physical_status(&pool, &status) && !memcmp(&status, &before, sizeof(status)));
    budget = 777;
    CHECK(!kernel_physical_budget_read(&pool, &budget) && budget == 777);
    CHECK(!kernel_physical_generation_read(&pool, &budget) && budget == 777);
    CHECK(!kernel_physical_budget_matches(&pool, 128) && !kernel_physical_budget_at_least(&pool, 0));
    CHECK(!kernel_physical_validate(&pool));
    physical_test_context_allowed(1);
    CHECK(ks_irq_state() == 0x200 && kernel_physical_validate(&pool));
    return 0;
}

// ====== context_private ======

static int context_private(void) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    KERNEL_LOCK_GUARD held = {0};
    KERNEL_LOCK outer = {0};
    KERNEL_PHYSICAL_STATUS status = {0};
    CHECK(context && !context->Depth);
    CHECK(!kernel_physical_status(&pool, (KERNEL_PHYSICAL_STATUS *)context));
    CHECK(context->Self == (uintptr_t)context && !context->Depth);
    CHECK(kernel_lock_acquire(&pool.Gate, context, &held));
    CHECK(!kernel_physical_validate(&pool) && !kernel_physical_status(&pool, &status));
    CHECK(!ks_irq_state() && context->Depth == 1);
    CHECK(kernel_lock_release(context, &held) && ks_irq_state() == 0x200);
    CHECK(kernel_lock_initialize(&outer, 100) && kernel_lock_acquire(&outer, context, &held));
    CHECK(!kernel_physical_status(&pool, (KERNEL_PHYSICAL_STATUS *)&held));
    CHECK(!kernel_physical_status(&pool, (KERNEL_PHYSICAL_STATUS *)&outer));
    CHECK(context->Depth == 1 && held.Self == (uintptr_t)&held);
    CHECK(kernel_lock_release(context, &held) && ks_irq_state() == 0x200);
    (void)ks_irq_save();
    CHECK(kernel_physical_status(&pool, &status) && !ks_irq_state());
    ks_irq_restore(0x200);
    return 0;
}

// ====== capacity_private ======

static int capacity_private(void) {
    KERNEL_PHYSICAL_LEASE lease;
    KERNEL_PHYSICAL_STATUS status;
    memset(&full, 0, sizeof(full));
    full.Self = (BOOT_U64)&full; full.Ready = 1; full.PhysicalBits = 32;
    full.Count = KERNEL_PHYSICAL_EXTENTS; full.NextId = 2;
    full.FreePages = KERNEL_PHYSICAL_EXTENTS; full.TotalPages = full.FreePages + 1;
    for (BOOT_U32 index = 0; index < full.Count; ++index)
        full.Free[index] = (KERNEL_PHYSICAL_EXTENT){4096 + (BOOT_U64)index * 0x10000, 1};
    lease = (KERNEL_PHYSICAL_LEASE){0x9000, 1, 1, (BOOT_U64)&full};
    full.Live[0] = lease; full.Pins[0] = 1;
    CHECK(kernel_lock_initialize(&full.Gate, KPHYS_LOCK_RANK));
    CHECK(kernel_physical_validate(&full));
    CHECK(!kernel_physical_release_fits(&full, &lease));
    CHECK(!kernel_physical_release_after_unmap(&full, &lease));
    CHECK(kernel_physical_is_pinned(&full, &lease) && lease.Id == 1);
    CHECK(kernel_physical_status(&full, &status) && status.PinnedPages == 1 && !status.Generation);
    lease.Base = 0x2000; full.Live[0] = lease;
    CHECK(kernel_physical_release_after_unmap(&full, &lease));
    CHECK(kernel_physical_status(&full, &status) && !status.LivePages && !status.PinnedPages);
    CHECK(status.FreePages == status.TotalPages && status.Generation == 2);
    return 0;
}

// ====== run_physical_query_tests ======

int run_physical_query_tests(void) {
    if (queries_private() || context_private() || capacity_private()) return 1;
    puts("PHYSICAL_QUERIES_PASS: snapshots ranges alias context IRQ recursion and retained pin on capacity failure; CPU model");
    return 0;
}

#include "arch/x86/kernel/cpu/percpu/transition/transition.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "faults.h"
#include "kernel/sync/internal.h"
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_AP_LOW_MEMORY code;
static X86_AP_TRANSITION owner;
static unsigned char map[96];
static BOOT_U64 root_page[512], pages[256][512];
static unsigned int calls, fail_at;
#define ROOT 0x500000ULL

// ====== field_private ======

static void field_private(unsigned int offset, BOOT_U64 value, unsigned int bytes) {
    for (unsigned int i = 0; i < bytes; ++i) { map[offset + i] = (unsigned char)value; value >>= 8; }
}

// ====== setup_private ======

static void setup_map_private(BOOT_U64 low_pages, unsigned int low_type, BOOT_U64 high_base) {
    memset(&pool, 0, sizeof(pool)); memset(&code, 0, sizeof(code)); memset(&owner, 0, sizeof(owner));
    memset(map, 0, sizeof(map)); memset(root_page, 0, sizeof(root_page)); memset(pages, 0xcd, sizeof(pages));
    field_private(0, low_type, 4); field_private(8, 0x1000, 8); field_private(24, low_pages, 8); field_private(32, 8, 8);
    field_private(48, 7, 4); field_private(56, high_base, 8); field_private(72, 4, 8); field_private(80, 8, 8);
    assert(kernel_physical_initialize(&pool, map, sizeof(map), 48, 40));
    root_page[510] = ROOT | (1ULL << 63) | 3; root_page[511] = 0x700003; root_page[488] = 0x710003;
    calls = fail_at = 0;
    xt_fault_reset();
}

// ====== setup_private ======

static void setup_private(void) { setup_map_private(255, 7, 0xffffe000ULL); }

// ====== safe_private ======

static int safe_private(void *context) { return context == &pool; }

// ====== transfer_private ======

static int transfer_private(void *context, BOOT_U64 physical, BOOT_U64 *page, int write) {
    assert(context == &pool && page && !(physical & 4095));
    if (++calls == fail_at) return 0;
    if (physical == ROOT) { assert(!write); memcpy(page, root_page, 4096); return 1; }
    assert(physical >= 4096 && physical < 0x100000);
    if (write) memcpy(pages[physical / 4096], page, 4096);
    else memcpy(page, pages[physical / 4096], 4096);
    return 1;
}

// ====== bounded_private ======

static void bounded_private(void) {
    setup_private();
    KERNEL_PHYSICAL_LEASE lease = {0};
    BOOT_U64 budget = pool.FreePages;
    assert(kernel_physical_allocate_within(&pool, 1, 1, 0xfffff000ULL, 0x100000000ULL, &lease));
    assert(lease.Base == 0xfffff000ULL && kernel_physical_release(&pool, &lease));
    assert(kernel_physical_budget_matches(&pool, budget));
    for (unsigned int i = 0; i < 5; ++i) {
        BOOT_U64 minimum[] = {0,4097,0x100000,4096,4096};
        BOOT_U64 limit[] = {0x100000,0x100000,0x100000,UINT64_MAX,0x10000000000000ULL};
        assert(!kernel_physical_allocate_within(&pool, 1, 1, minimum[i], limit[i], &lease));
        assert(!lease.Id && kernel_physical_budget_matches(&pool, budget));
    }
    assert(kernel_physical_allocate_within(&pool, 2, 4, 0x3000, 0xa000, &lease));
    assert(lease.Base == 0x4000 && kernel_physical_release(&pool, &lease));
}

// ====== lifecycle_private ======

static unsigned int lifecycle_private(unsigned int failure) {
    setup_private();
    BOOT_U64 budget = pool.FreePages;
    assert(x86_ap_low_reserve(&pool, &code));
    assert(xt_early(&pool, &code, &owner) && owner.State == 1 && owner.Data.Base == 0x2000);
    assert(!xt_early(&pool, &code, &owner));
    XT_IO io = {transfer_private, safe_private, &pool};
    assert(!xt_prepare(&owner, owner.Data.Base, 6, &io) && !owner.Tables.Id);
    fail_at = failure;
    if (failure) {
        assert(!xt_prepare(&owner, ROOT, 6, &io) && !owner.Tables.Id && owner.State == 1);
        assert(kernel_physical_budget_matches(&pool, budget - 2));
        fail_at = calls = 0;
    }
    assert(xt_prepare(&owner, ROOT, 6, &io) && owner.State == 3 && xt_verify(&owner));
    unsigned int total = calls;
    assert(owner.Tables.Pages == 4 && owner.Tables.Base == 0x3000);
    assert(!xt_prepare(&owner, ROOT, 6, &io));
    assert(!x86_ap_low_release(&code) && !x86_ap_low_unhold(&code, &pool));
    KERNEL_PHYSICAL_LEASE saved_data = owner.Data, saved_tables = owner.Tables;
    owner.Data.Id = owner.Tables.Id = 0;
    owner.Data.Pages = UINT64_MAX; owner.Tables.Pages = 1;
    owner.DataPinned = owner.TablesPinned = 0;
    assert(!xt_cancel(&owner) && owner.Self == (BOOT_U64)&owner);
    owner.Data = saved_data; owner.Tables = saved_tables;
    owner.DataPinned = owner.TablesPinned = 1;
    assert(xt_verify(&owner));
    X86_AP_TRANSITION copy = owner;
    assert(!xt_cancel(&copy));
    pages[owner.Tables.Base / 4096][0] ^= 4;
    assert(!xt_verify(&owner)); pages[owner.Tables.Base / 4096][0] ^= 4;
    root_page[480] = 0x730003;
    assert(!xt_verify(&owner)); root_page[480] = 0;
    root_page[511] |= 0x60;
    assert(xt_verify(&owner));
    xt_release_calls = 0; xt_fail_release = 2;
    assert(!xt_cancel(&owner) && owner.State == 1 && !owner.Tables.Id && owner.DataPinned == 1);
    assert(x86_ap_low_is_held(&code, &owner));
    xt_fail_release = 0;
    assert(xt_cancel(&owner) && kernel_physical_budget_matches(&pool, budget - 1));
    assert(x86_ap_low_release(&code) && kernel_physical_budget_matches(&pool, budget));
    assert(kernel_physical_validate(&pool));
    return total;
}

// ====== metadata_failures_private ======

static void metadata_failures_private(void) {
    for (unsigned int kind = 0; kind < 2; ++kind) for (unsigned int phase = 1; phase <= 2; ++phase) {
        setup_private();
        BOOT_U64 budget = pool.FreePages;
        assert(x86_ap_low_reserve(&pool, &code));
        if (kind) xt_fail_pin = phase; else xt_fail_alloc = phase;
        assert(xt_early(&pool, &code, &owner));
        XT_IO io = {transfer_private, safe_private, &pool};
        if (phase == 1) assert(owner.State == 2 && !owner.Data.Id);
        else assert(xt_prepare(&owner, ROOT, 6, &io) == (kind ? 0 : 2) && !owner.Tables.Id);
        xt_fail_alloc = xt_fail_pin = 0;
        assert(xt_cancel(&owner) && x86_ap_low_release(&code));
        assert(kernel_physical_budget_matches(&pool, budget));
    }
    setup_private();
    BOOT_U64 budget = pool.FreePages;
    assert(x86_ap_low_reserve(&pool, &code) && xt_early(&pool, &code, &owner));
    XT_IO io = {transfer_private, safe_private, &pool};
    fail_at = 2; xt_fail_release = xt_stop_armed = 1;
    if (!setjmp(xt_terminal)) { (void)xt_prepare(&owner, ROOT, 6, &io); assert(0); }
    assert(!strcmp(xt_reason, "AP_TRANSITION_TABLE_ROLLBACK_FAILED"));
    assert(kernel_physical_lease_state(&pool, &owner.Tables, 1) && kernel_physical_lease_state(&pool, &owner.Data, 1));
    xt_fail_release = xt_stop_armed = 0;
    assert(xt_cancel(&owner) && x86_ap_low_release(&code));
    assert(kernel_physical_budget_matches(&pool, budget));
    setup_private(); budget = pool.FreePages;
    assert(x86_ap_low_reserve(&pool, &code) && x86_ap_low_hold(&code, &pool));
    assert(!xt_early(&pool, &code, &owner) && !owner.Data.Id);
    assert(xt_cancel(&owner) && !x86_ap_low_release(&code));
    assert(x86_ap_low_unhold(&code, &pool) && x86_ap_low_release(&code));
    assert(kernel_physical_budget_matches(&pool, budget));
}

// ====== main ======

// ====== unavailable_private ======

static void unavailable_private(void) {
    for (unsigned int mode = 0; mode < 3; ++mode) {
        setup_map_private(mode == 2 ? 5 : mode == 1 ? 1 : 255, mode ? 7 : 2, 0x100000000ULL);
        BOOT_U64 budget = pool.FreePages;
        assert(x86_ap_low_reserve(&pool, &code) && xt_early(&pool, &code, &owner));
        if (mode < 2) assert(owner.State == 2 && !owner.Data.Id);
        else {
            XT_IO io = {transfer_private, safe_private, &pool};
            assert(owner.State == 1 && xt_prepare(&owner, ROOT, 6, &io) == 2);
        }
        assert(xt_cancel(&owner) && x86_ap_low_release(&code));
        assert(kernel_physical_budget_matches(&pool, budget));
    }
}

// ====== main ======

// ====== freeze_private ======

static void freeze_private(void) {
    setup_private();
    uint64_t saved_if = ks_irq_save();
    BOOT_U64 budget = pool.FreePages;
    assert(x86_ap_low_reserve(&pool, &code) && xt_early(&pool, &code, &owner));
    XT_IO io = {transfer_private, safe_private, &pool};
    assert(!xt_freeze(&owner));
    assert(xt_prepare(&owner, ROOT, 6, &io) == 1);
    xt_is_bsp = 0;
    assert(!xt_freeze(&owner) && owner.State == 3);
    xt_is_bsp = 1;
    X86_AP_TRANSITION copy = owner;
    assert(!xt_freeze(&copy));
    for (unsigned int failure = 1; failure <= 6; ++failure) {
        calls = 0; fail_at = failure;
        assert(!xt_freeze(&owner) && owner.State == 3);
        fail_at = 0;
        assert(xt_verify(&owner) && kernel_physical_budget_matches(&pool, budget - 6));
    }
    root_page[480] = 0x730003;
    assert(!xt_freeze(&owner) && owner.State == 3);
    root_page[480] = 0;
    assert(xt_freeze(&owner) && owner.State == 4 && xt_verify(&owner));
    calls = xt_release_calls = 0;
    assert(!xt_freeze(&owner) && !xt_cancel(&owner) && !xt_build(&owner));
    assert(!xt_prepare(&owner, ROOT, 6, &io));
    assert(!x86_ap_low_unhold(&code, &owner) && !x86_ap_low_release(&code));
    assert(!x86_ap_low_freeze(&code, &owner));
    assert(x86_ap_low_is_frozen(&code, &owner));
    owner.State = 1;
    assert(!xt_cancel(&owner) && !xt_build(&owner));
    owner.State = 3;
    assert(!xt_cancel(&owner) && !xt_verify(&owner));
    owner.State = 4;
    assert(!calls && !xt_release_calls && kernel_physical_budget_matches(&pool, budget - 6));
    assert(kernel_physical_lease_state(&pool, &code.Lease, 1));
    assert(kernel_physical_lease_state(&pool, &owner.Data, 1));
    assert(kernel_physical_lease_state(&pool, &owner.Tables, 1));
    pages[owner.Tables.Base / 4096][0] ^= 4;
    assert(!xt_verify(&owner) && !xt_cancel(&owner) && !x86_ap_low_unhold(&code, &owner));
    ks_irq_restore(saved_if);
    puts("PASS: irreversible inactive lifetime freeze; six pinned pages retained, no rebuild/release after freeze");
}

// ====== main ======

int main(int argc, char **argv) {
    if (argc == 2) { assert(!strcmp(argv[1], "freeze")); freeze_private(); return 0; }
    assert(argc == 1);
    bounded_private();
    (void)lifecycle_private(0);
    for (unsigned int failure = 1; failure <= 13; ++failure) (void)lifecycle_private(failure);
    metadata_failures_private();
    unavailable_private();
    puts("PASS: transition lifecycle, 13 I/O failure points, exact permissions, rollback and bounded allocation; CPU/mapping modeled");
    return 0;
}

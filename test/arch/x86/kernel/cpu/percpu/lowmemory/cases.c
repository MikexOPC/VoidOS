#include "arch/x86/kernel/cpu/percpu/lowmemory/lowmemory.h"
#include "kernel/sync/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_AP_LOW_MEMORY low;
static unsigned char map[96];

// ====== ks_boot_cpu ======

uint32_t ks_boot_cpu(void) { return 1; }

// ====== field_private ======

static void field_private(unsigned int offset, BOOT_U64 value, unsigned int bytes) {
    for (unsigned int i = 0; i < bytes; ++i) { map[offset + i] = (unsigned char)value; value >>= 8; }
}

// ====== setup_private ======

static void setup_private(BOOT_U64 base, BOOT_U64 pages, unsigned int type) {
    memset(&pool, 0, sizeof(pool)); memset(&low, 0, sizeof(low)); memset(map, 0, sizeof(map));
    field_private(0, type, 4); field_private(8, base, 8); field_private(24, pages, 8); field_private(32, 8, 8);
    field_private(48, 7, 4); field_private(56, 0x200000, 8); field_private(72, 1024, 8); field_private(80, 8, 8);
    assert(kernel_physical_initialize(&pool, map, sizeof(map), 48, 40));
}

// ====== reserve_private ======

static void reserve_private(BOOT_U64 base, BOOT_U64 pages, BOOT_U64 expected) {
    setup_private(base, pages, 7);
    BOOT_U64 budget = pool.FreePages;
    assert(x86_ap_low_reserve(&pool, &low) && x86_ap_low_valid(&low));
    assert(low.State == 2 && low.Lease.Base == expected && low.Lease.Pages == 1 && low.Lease.Id == 1);
    assert(pool.FreePages == budget - 1 && pool.NextId == 2 && kernel_physical_is_pinned(&pool, &low.Lease));
    assert(!x86_ap_low_reserve(&pool, &low));
    KERNEL_PHYSICAL_LEASE stale = low.Lease, wrong = low.Lease;
    X86_AP_LOW_MEMORY copied = low;
    assert(!x86_ap_low_release(&copied));
    wrong.Owner = 0; assert(!kernel_physical_release(&pool, &wrong));
    assert(!kernel_physical_release(&pool, &stale));
    KERNEL_PHYSICAL_LEASE other = {0};
    assert(kernel_physical_allocate(&pool, 1, &other) && other.Base != expected);
    assert(kernel_physical_release(&pool, &other));
    assert(x86_ap_low_release(&low) && !low.Self && pool.FreePages == budget);
    assert(!x86_ap_low_release(&low) && !kernel_physical_release(&pool, &stale));
    assert(!x86_ap_low_reserve(&pool, &low));
    assert(kernel_physical_validate(&pool));
}

// ====== unavailable_private ======

static void unavailable_private(BOOT_U64 base, BOOT_U64 pages, unsigned int type) {
    setup_private(base, pages, type);
    BOOT_U64 budget = pool.FreePages;
    assert(x86_ap_low_reserve(&pool, &low) && low.State == 1 && x86_ap_low_valid(&low));
    assert(!low.Lease.Id && pool.NextId == 1 && pool.FreePages == budget);
    assert(!x86_ap_low_reserve(&pool, &low));
    assert(x86_ap_low_release(&low) && pool.FreePages == budget);
}

// ====== main ======

int main(void) {
    for (unsigned int repeat = 0; repeat < 10; ++repeat) {
        reserve_private(0, 2, 4096);
        reserve_private(0x9000, 2, 0x9000);
        reserve_private(0xff000, 2, 0xff000);
        unavailable_private(0x100000, 2, 7);
        unavailable_private(0x8000, 2, 2);
        unavailable_private(0, 1, 7);
        setup_private(0x9000, 2, 7);
        assert(!x86_ap_low_reserve(NULL, &low) && !x86_ap_low_reserve(&pool, NULL));
        assert(!x86_ap_low_reserve(&pool, (X86_AP_LOW_MEMORY *)&pool));
        assert(!x86_ap_low_reserve(&pool, (X86_AP_LOW_MEMORY *)kernel_lock_current_context()));
        low.Reserved = 1;
        assert(!x86_ap_low_reserve(&pool, &low) && pool.NextId == 1);
        memset(&low, 0, sizeof(low));
        KERNEL_PHYSICAL_LEASE other = {0};
        assert(kernel_physical_allocate(&pool, 1, &other));
        assert(!x86_ap_low_reserve(&pool, &low) && !low.Self);
        assert(kernel_physical_release(&pool, &other) && kernel_physical_validate(&pool));
    }
    puts("PASS: low-memory reservation, 10 repetitions; real allocator, modeled IRQ/context, C ASan");
    return 0;
}

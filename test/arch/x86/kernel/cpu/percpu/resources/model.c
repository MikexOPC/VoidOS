#include "model.h"
#include "kernel/sync/internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned int calls, fail_at, live, pins, cpus_model, alias_model, bad_flush, bad_release;
BOOT_U64 root_model[512];
KERNEL_BOOT_CONTEXT context_model;
KERNEL_PHYSICAL_ALLOCATOR pool_model;
X86_PAGE_WINDOW window_model;
X86_PAGING_INVENTORY inventory_model;
static _Alignas(4096) unsigned char data[1827 * 4096], tables[10 * 4096];
static void *window_bytes;
static unsigned int allocated[2], pinned[2], published, flushed;
static BOOT_U64 alias_entries[512];

// ====== step_private ======

static int step_private(void) {
    if (++calls != fail_at) return 1;
    fail_at = 0; return 0;
}

// ====== model_reset ======

void model_reset(void) {
    assert(!x86_ap_owner()); calls = fail_at = live = pins = alias_model = bad_flush = bad_release = 0;
    published = flushed = 0; memset(allocated, 0, sizeof(allocated)); memset(pinned, 0, sizeof(pinned));
    memset(root_model, 0, sizeof(root_model)); memset(&context_model, 0, sizeof(context_model));
    memset(&pool_model, 0, sizeof(pool_model)); memset(&window_model, 0, sizeof(window_model));
    memset(&inventory_model, 0, sizeof(inventory_model)); memset(alias_entries, 0, sizeof(alias_entries));
    memset(data, 0xa5, sizeof(data)); memset(tables, 0xa5, sizeof(tables));
    context_model.Info.PageTableRoot = 0x8000;
    pool_model.PhysicalBits = 48; pool_model.FreePages = 4096;
    window_model.Pool = &pool_model; window_model.Ready = 1;
}

// ====== cpu_and_window_model ======

uint32_t ks_boot_cpu(void) { return 1; }
uint64_t ks_irq_state(void) { return 0; }
__declspec(noreturn) void ks_fail_stop(const char *reason) { fprintf(stderr, "Unexpected terminal: %s\n", reason); exit(1); }
int x86_fb_cpu(X86_FB_CPU *cpu) {
    if (!step_private()) return 0;
    *cpu = (X86_FB_CPU){0x8000, 0x0007040600070406ULL, 48, 0}; return 1;
}
volatile BOOT_U64 *x86_fb_root(void) { return root_model; }
int x86_fb_flush(BOOT_U64 root) {
    assert(root == 0x8000);
    if (root_model[X86_AP_SLOT]) published = 1;
    if (bad_flush) { bad_flush = 0; return 0; }
    if (!step_private()) return 0;
    if (!root_model[X86_AP_SLOT]) flushed = 1;
    return 1;
}
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *window) { return window && window->Ready == 1 && !window->Active; }
volatile unsigned char *x86_window_bytes(void) { return window_bytes; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address) { (void)address; return alias_entries; }
int x86_apic_borrow(X86_PAGE_WINDOW *window, BOOT_U64 physical, BOOT_U64 cache, int writable,
    int (*operation)(void *), void *state) {
    assert(window == &window_model && !window->Active && !cache && writable == 1);
    if (!step_private()) return 0;
    if (physical >= 0x2000000) {
        assert(physical - 0x2000000 < sizeof(tables)); window_bytes = tables + (physical - 0x2000000);
    } else {
        assert(physical >= 0x1000000 && physical - 0x1000000 < sizeof(data)); window_bytes = data + (physical - 0x1000000);
    }
    window->Active = 2; int result = operation(state); window->Active = 0; window_bytes = NULL; return result;
}

// ====== physical_lease_model ======

int kernel_physical_budget_read(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 *value) { *value = pool->FreePages; return 1; }
int kernel_physical_budget_at_least(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 pages) { return pool->FreePages >= pages; }
int kernel_physical_budget_matches(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 pages) { return pool->FreePages == pages; }
int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *lease) {
    if (!step_private()) return 0;
    unsigned int slot = allocated[0] ? 1 : 0;
    assert(!allocated[slot] && pool->FreePages >= pages);
    allocated[slot] = 1; ++live; pool->FreePages -= pages;
    *lease = (KERNEL_PHYSICAL_LEASE){slot ? 0x2000000 : 0x1000000, pages, slot + 1, (BOOT_U64)pool}; return 1;
}
int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *pool, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin) {
    (void)pool; assert(pin == 1 && lease->Id >= 1 && lease->Id <= 2);
    if (!step_private()) return 0;
    pinned[lease->Id - 1] = 1; ++pins; return 1;
}
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *pool, const KERNEL_PHYSICAL_LEASE *lease) {
    return pool == &pool_model && lease->Id >= 1 && lease->Id <= 2 && allocated[lease->Id - 1] && pinned[lease->Id - 1];
}
int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *pool, KERNEL_PHYSICAL_LEASE *lease) {
    assert(lease->Id >= 1 && lease->Id <= 2 && !pinned[lease->Id - 1]);
    assert(!root_model[X86_AP_SLOT] && (!published || flushed));
    allocated[lease->Id - 1] = 0; --live; pool->FreePages += lease->Pages; memset(lease, 0, sizeof(*lease)); return 1;
}
int kernel_physical_release_after_unmap(KERNEL_PHYSICAL_ALLOCATOR *pool, KERNEL_PHYSICAL_LEASE *lease) {
    assert(kernel_physical_is_pinned(pool, lease));
    if (bad_release && --bad_release == 0) return 0;
    pinned[lease->Id - 1] = 0; --pins; return kernel_physical_release(pool, lease);
}

// ====== capture_model ======

int capture_model(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory) {
    (void)context; (void)lapic;
    if (!step_private()) return 0;
    if (!x86_ap_inputs(pool, 0x8000, (BOOT_U64)inventory, sizeof(*inventory), &inventory->Ap)) return 0;
    if (alias_model && !x86_ap_owner()) {
        inventory->Count = 1; inventory->Tables[0] = (X86_PAGING_RESOURCE){0x30000, 0x40000, 3, 0};
        alias_entries[0] = 0x1000003;
    }
    if (!x86_ap_owner()) return 1;
    const X86_AP_AUTH *auth = x86_ap_owner();
    assert(auth->Cpus == cpus_model && auth->Tables.Pages == xa_geometry_tables(cpus_model));
    for (unsigned int page = 0; page < auth->Tables.Pages; ++page) {
        const BOOT_U64 *entries = (const BOOT_U64 *)(tables + page * 4096);
        for (unsigned int slot = 0; slot < 512; ++slot) {
            BOOT_U64 expected;
            if (!page) expected = slot ? 0 : 0x2001003;
            else if (page == 1) expected = slot && slot <= auth->Tables.Pages - 2 ? (0x2000000 + (slot + 1ULL) * 4096) | 3 : 0;
            else {
                unsigned int index = xa_geometry_page(cpus_model, (page - 2ULL) * 512 + slot);
                expected = index == UINT32_MAX ? 0 : (0x1000000 + index * 4096ULL) | X86_AP_NX | 3;
                if (expected) assert(x86_ap_leaf(expected, X86_AP_BASE + ((page - 2ULL) * 512 + slot) * 4096) == 10);
            }
            assert(entries[slot] == expected);
        }
    }
    return x86_ap_complete(&inventory->Ap, (1U << auth->Tables.Pages) - 1, auth->Data.Pages);
}

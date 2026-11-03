#include "binding.h"
#ifdef KERNEL_AP_RUNTIME
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
typedef struct { const X86_HEAP_BINDING *Owner; BOOT_U32 Index; } HEAP_WRITE;
static X86_HEAP_BINDING binding;
static KERNEL_HEAP heap;
static BOOT_U32 started;

// ====== x86_heap_check ======

int x86_heap_check(const X86_HEAP_AUTH *auth)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root = x86_fb_root();

    return auth && auth->Self == (BOOT_U64)auth && auth->Ready == 1 &&
        x86_fb_cpu(&cpu) && cpu.Root == auth->Root && cpu.Pat == auth->Pat &&
        cpu.PhysicalBits == auth->PhysicalBits && root &&
        (root[X86_HEAP_SLOT] & ~0x60ULL) == (auth->Tables.Base | 3);
}

// ====== entry_private ======

// Intel SDM Vol. 3A 5.5.4/5.6, 4-KiB pages: P0, RW1, US2=0 at every level; leaves XD63, WB (PAT 0).
// PDPT slot 0 -> PD; PD slots 1..8 -> the eight PTs (PD slots 0 and 9 stay zero as guards);
// PT k slot s -> data page k * 512 + s.
static BOOT_U64 entry_private(const X86_HEAP_AUTH *auth, BOOT_U32 table, BOOT_U32 slot)
{
    if (table == 0) return slot ? 0 : (auth->Tables.Base + 4096) | 3;

    if (table == 1) return slot >= 1 && slot <= 8 ? (auth->Tables.Base + (slot + 1) * 4096ULL) | 3 : 0;

    return (auth->Data.Base + ((table - 2) * 512ULL + slot) * 4096) | 0x8000000000000003ULL;
}

// ====== write_private ======

static int write_private(void *opaque)
{
    const HEAP_WRITE *request = opaque;
    volatile BOOT_U64 *view = (volatile BOOT_U64 *)x86_window_bytes();

    if (!view) return 0;

    for (BOOT_U32 slot = 0; slot < 512; ++slot) {
        BOOT_U64 entry = entry_private(&request->Owner->Authority, request->Index, slot);

        view[slot] = entry;

        if (view[slot] != entry) return 0;
    }

    return 1;
}

// ====== unreferenced_private ======

// No page table and no existing leaf may already name a frame of the range (no alias).
static int unreferenced_private(const X86_PAGING_INVENTORY *inventory, BOOT_U64 base, BOOT_U64 pages)
{
    for (BOOT_U32 index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];

        if (kernel_regions_overlap(record->Physical, 1, base, pages)) return 0;

        if (record->Level != 3) continue;

        const volatile BOOT_U64 *entries = x86_paging_view(record->View);

        if (!entries) return 0;

        for (BOOT_U32 slot = 0; slot < 512; ++slot)
            if (entries[slot] && kernel_regions_overlap(entries[slot] & 0x000ffffffffff000ULL, 1, base, pages)) return 0;
    }

    return 1;
}

// ====== construct_private ======

// The data pages are not zeroed here: the heap core zeroes every allocation before handing it out.
static int construct_private(X86_HEAP_BINDING *owner)
{
    HEAP_WRITE request = {owner, 0};

    if (!unreferenced_private(owner->Inventory, owner->Authority.Data.Base, X86_HEAP_PAGES) ||
        !unreferenced_private(owner->Inventory, owner->Authority.Tables.Base, X86_HEAP_TABLES)
    )
    return 0;

    for (request.Index = 0; request.Index < X86_HEAP_TABLES; ++request.Index)
        if (!x86_apic_borrow(owner->Window, owner->Authority.Tables.Base + request.Index * 4096ULL, 0, 1, write_private, &request) ||
            !x86_page_window_idle_valid(owner->Window)
        )
        return 0;

    return 1;
}

// ====== x86_heap_unbind ======

// Reverse of bind: unpublish and flush, unregister, capture, then release tables and data.
int x86_heap_unbind(X86_HEAP_BINDING *owner)
{
    X86_FB_CPU cpu;

    if (!owner || owner->Self != (BOOT_U64)owner || !owner->Window || !owner->Window->Pool ||
        !x86_fb_cpu(&cpu) || cpu.Root != owner->Authority.Root || cpu.Pat != owner->Authority.Pat ||
        !x86_page_window_idle_valid(owner->Window)
    )
    return 0;

    volatile BOOT_U64 *root = x86_fb_root();
    KERNEL_PHYSICAL_ALLOCATOR *pool = owner->Window->Pool;

    if (!root) return 0;

    if (owner->Published) {
        if (root[X86_HEAP_SLOT] && (root[X86_HEAP_SLOT] & ~0x60ULL) != (owner->Authority.Tables.Base | 3)) return 0;

        root[X86_HEAP_SLOT] = 0;

#ifdef KERNEL_AP_RUNTIME
        if (xis_native_shootdown()) x86_paging_generation_bump();
        else
#endif
        if (!x86_fb_flush(cpu.Root)) return 0;
        if (root[X86_HEAP_SLOT]) return 0;

        owner->Published = 0;
    }
    else if (root[X86_HEAP_SLOT]) return 0;

    if (owner->Registered) {
        if (!x86_heap_register(&owner->Authority, 0, 0)) return 0;

        owner->Registered = 0;
    }
    else if (x86_heap_owner() == &owner->Authority) return 0;

    owner->Authority.Ready = 0;

    if (!owner->Capture(owner->Context, pool, owner->Lapic, owner->Inventory)) return 0;

    if (owner->TablesPinned) { if (!kernel_physical_pin(pool, &owner->Authority.Tables, 0)) return 0; owner->TablesPinned = 0; }

    if (owner->TablesAllocated) { if (!kernel_physical_release(pool, &owner->Authority.Tables)) return 0; owner->TablesAllocated = 0; }

    if (owner->DataPinned) { if (!kernel_physical_pin(pool, &owner->Authority.Data, 0)) return 0; owner->DataPinned = 0; }

    if (owner->DataAllocated) { if (!kernel_physical_release(pool, &owner->Authority.Data)) return 0; owner->DataAllocated = 0; }

    if (!kernel_physical_budget_matches(pool, owner->Budget)) return 0;

    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;

    return 1;
}

// ====== x86_heap_bind ======

int x86_heap_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory, X86_HEAP_CAPTURE capture, X86_HEAP_BINDING *owner)
{
    X86_FB_CPU cpu;
    BOOT_U64 self = (BOOT_U64)owner;

    if (!context || !window || !window->Pool || !inventory || !capture || !owner || (self & 7) || x86_heap_owner() ||
        os_address_ranges_overlap(self, sizeof(*owner), (BOOT_U64)inventory, sizeof(*inventory)) ||
        os_address_ranges_overlap(self, sizeof(*owner), X86_HEAP_VIRTUAL, X86_HEAP_PAGES * 4096ULL) ||
        !x86_page_window_idle_valid(window) || !x86_fb_cpu(&cpu) || (cpu.Pat & 255) != 6 ||
        cpu.Root != context->Info.PageTableRoot || !x86_fb_root() || x86_fb_root()[X86_HEAP_SLOT]
    )
    return 0;

    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;

    if (!capture(context, window->Pool, lapic, inventory) || !kernel_physical_budget_at_least(window->Pool, X86_HEAP_PAGES + X86_HEAP_TABLES)) return 0;

    KERNEL_PHYSICAL_ALLOCATOR *pool = window->Pool;

    owner->Self = self; if (!kernel_physical_budget_read(pool, &owner->Budget)) return 0;
    owner->Context = context; owner->Window = window; owner->Lapic = lapic; owner->Inventory = inventory; owner->Capture = capture;
    owner->Authority.Self = (BOOT_U64)&owner->Authority; owner->Authority.Root = cpu.Root;
    owner->Authority.Pat = cpu.Pat; owner->Authority.PhysicalBits = cpu.PhysicalBits;

    if (!kernel_physical_allocate(pool, X86_HEAP_PAGES, &owner->Authority.Data)) goto rollback;
    owner->DataAllocated = 1;
    if (!kernel_physical_pin(pool, &owner->Authority.Data, 1)) goto rollback;
    owner->DataPinned = 1;
    if (!kernel_physical_allocate(pool, X86_HEAP_TABLES, &owner->Authority.Tables)) goto rollback;
    owner->TablesAllocated = 1;
    if (!kernel_physical_pin(pool, &owner->Authority.Tables, 1)) goto rollback;
    owner->TablesPinned = 1;

    if (!construct_private(owner)) goto rollback;

    owner->Published = 1;
    x86_fb_root()[X86_HEAP_SLOT] = owner->Authority.Tables.Base | 3;

    if (!x86_fb_flush(cpu.Root)) goto rollback;

    owner->Authority.Ready = 1;

    if (!x86_heap_register(0, &owner->Authority, x86_heap_check)) goto rollback;

    owner->Registered = 1;

    if (!capture(context, pool, lapic, inventory)) goto rollback;

    return 1;

rollback:
    (void)x86_heap_unbind(owner);
    return 0;
}

// ====== selftest_private ======

#ifdef KERNEL_TEST_KERNEL_HEAP
// Test image: small objects of every class, a page run and a large run, written and freed;
// a double free and a corrupted canary rejected; everything back to free at the end.
static int selftest_private(void)
{
    unsigned char *small[64], *run, *large;

    for (BOOT_U32 index = 0; index < 64; ++index) {
        small[index] = kernel_heap_alloc(&heap, 8 + index * 31, 0);
        if (!small[index]) return 0;
        for (BOOT_U32 byte = 0; byte < 8 + index * 31; ++byte) small[index][byte] = (unsigned char)index;
    }

    run = kernel_heap_pages(&heap, 3);
    large = kernel_heap_alloc(&heap, 640 * 480 * 4, 4096);
    if (!run || !large || ((BOOT_U64)large & 4095)) return 0;

    for (BOOT_U64 byte = 0; byte < 640 * 480 * 4; byte += 4096) large[byte] = 0x5a;

    for (BOOT_U32 index = 0; index < 64; ++index) {
        for (BOOT_U32 byte = 0; byte < 8 + index * 31; ++byte) if (small[index][byte] != (unsigned char)index) return 0;
        if (!kernel_heap_free(&heap, small[index]) || kernel_heap_free(&heap, small[index])) return 0;
    }

    unsigned char *overflow = kernel_heap_alloc(&heap, 24, 0);
    if (!overflow) return 0;
    for (BOOT_U32 byte = 0; byte < 32; ++byte) overflow[byte] = 0x41;

    KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
    if (kernel_heap_free(&heap, overflow) || kernel_heap_stats(&heap).Rejected != stats.Rejected + 1) return 0;

    if (!kernel_heap_free(&heap, run) || !kernel_heap_free(&heap, large)) return 0;

    stats = kernel_heap_stats(&heap);
    // 64 double frees and the corrupted canary: Rejected == 65.
    return stats.Objects == 1 && stats.RunPages == 0 && stats.SlabPages == 1 && !stats.Pending && kernel_heap_valid(&heap) &&
        kernel_text("KERNEL_HEAP_SELFTEST_REJECTED: ") && kernel_number(stats.Rejected) && kernel_text("\n") &&
        kernel_log(0, "KERNEL_HEAP_SELFTEST_OK");
}
#endif

// ====== x86_heap_kernel_start ======

int x86_heap_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory)
{
    if (started || !kernel_section("heap kernel") ||
        !x86_heap_bind(context, window, lapic, inventory, x86_paging_inventory_capture_lapic, &binding) ||
        !kernel_heap_init(&heap, (void *)X86_HEAP_VIRTUAL, X86_HEAP_PAGES)
    )
    return 0;

    started = 1;
#ifdef KERNEL_TEST_KERNEL_HEAP
    if (!selftest_private()) return 0;
#endif
    if (!kernel_heap_valid(&heap) || kernel_heap_stats(&heap).Pending || !kernel_log(0, "KERNEL_HEAP_SYNC_OK")) return 0;
    // K1 criterion 1: what is still mapped below slot 256 once the kernel runs in the upper half.
    BOOT_U32 lower_first = 0, lower = x86_paging_lower_slots(&lower_first);

    if (!kernel_text("KERNEL_LOWER_HALF_SLOTS: ") || !kernel_number(lower) ||
        !kernel_text("\nKERNEL_LOWER_HALF_FIRST: ") || !kernel_number(lower_first) || !kernel_text("\n") ||
        !kernel_log(lower ? 2 : 0, lower ? "KERNEL_LOWER_HALF_USED" : "KERNEL_LOWER_HALF_EMPTY")
    )
    return 0;

    return kernel_text("KERNEL_HEAP_VIRTUAL: ") && kernel_number(X86_HEAP_VIRTUAL) &&
        kernel_text("\nKERNEL_HEAP_PAGES: ") && kernel_number(X86_HEAP_PAGES) &&
        kernel_text("\nKERNEL_HEAP_TABLES: ") && kernel_number(X86_HEAP_TABLES) &&
        kernel_text("\nKERNEL_HEAP_DATA: ") && kernel_number(binding.Authority.Data.Base) &&
        kernel_text("\nKERNEL_HEAP_INVENTORY: ") && kernel_number((BOOT_U64)inventory) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_HEAP_READY");
}

// ====== x86_heap_tamper ======

#ifdef KERNEL_TEST_PAGING_TAMPER
// Test-only: clears XD of the first heap leaf through the recursive view of its PT, outside the heap
// APIs (no authority update, no invlpg, no generation change). The next paging audit must reject
// it (G5); until then the stale TLB entry keeps the old permissions.
int x86_heap_tamper(void)
{
    const X86_PAGING_INVENTORY *inventory = binding.Inventory;

    if (!started || !inventory) return 0;

    for (BOOT_U32 index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];

        if (record->Reserved != X86_HEAP_TAG + 2) continue;

        volatile BOOT_U64 *entries = (volatile BOOT_U64 *)x86_paging_view(record->View);

        if (!entries) return 0;

        entries[0] &= ~0x8000000000000000ULL;
        return kernel_log(2, "KERNEL_PAGING_TAMPERED");
    }

    return 0;
}
#endif

// ====== kernel_heap ======

KERNEL_HEAP *kernel_heap(void) { return started ? &heap : 0; }

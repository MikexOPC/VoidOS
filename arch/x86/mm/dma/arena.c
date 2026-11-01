#include "arena.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "lib/memory/address_ranges.h"
typedef struct { const X86_DMA_ARENA *Owner; BOOT_U32 Index; } ARENA_WRITE;

// ====== x86_dma_arena_check ======

int x86_dma_arena_check(const X86_DMA_AUTH *auth)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root = x86_fb_root();

    return auth && auth->Self == (BOOT_U64)auth && auth->Ready == 1 &&
        x86_fb_cpu(&cpu) && cpu.Root == auth->Root && cpu.Pat == auth->Pat &&
        cpu.PhysicalBits == auth->PhysicalBits && root &&
        (root[X86_DMA_SLOT] & ~0x60ULL) == (auth->Tables[0].Base | 3);
}

// ====== entry_private ======

// Intel SDM Vol. 3A 5.5.4/5.6, 4-KiB pages: P0, RW1, US2=0 at every level; leaves XD63, WB (PAT 0).
// PDPT and PD use slot 0; the PT maps slot 1 to pages, slot 0 and pages+1 stay zero as guards.
static BOOT_U64 entry_private(const X86_DMA_AUTH *auth, BOOT_U32 table, BOOT_U32 slot)
{
    if (table < 2) return slot ? 0 : auth->Tables[table + 1].Base | 3;

    if (!slot || slot > auth->Data.Pages) return 0;

    return (auth->Data.Base + (slot - 1) * 4096ULL) | 0x8000000000000003ULL;
}

// ====== write_private ======

static int write_private(void *opaque)
{
    const ARENA_WRITE *request = opaque;
    volatile BOOT_U64 *view = (volatile BOOT_U64 *)x86_window_bytes();

    if (!view) return 0;

    for (BOOT_U32 slot = 0; slot < 512; ++slot) {
        BOOT_U64 entry = entry_private(&request->Owner->Authority, request->Index, slot);

        view[slot] = entry;

        if (view[slot] != entry) return 0;
    }

    return 1;
}

// ====== zero_private ======

static int zero_private(void *opaque)
{
    volatile BOOT_U64 *view = (volatile BOOT_U64 *)x86_window_bytes();

    (void)opaque;

    if (!view) return 0;

    for (BOOT_U32 slot = 0; slot < 512; ++slot) view[slot] = 0;

    for (BOOT_U32 slot = 0; slot < 512; ++slot) if (view[slot]) return 0;

    return 1;
}

// ====== unreferenced_private ======

// No page table and no existing leaf may already name the frame (no alias of the arena).
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

static int construct_private(X86_DMA_ARENA *owner)
{
    ARENA_WRITE request = {owner, 0};

    if (!unreferenced_private(owner->Inventory, owner->Authority.Data.Base, owner->Authority.Data.Pages)) return 0;

    // The arena is zeroed through window borrows before it is published: no stale data reaches a device.
    for (BOOT_U64 page = 0; page < owner->Authority.Data.Pages; ++page)
        if (!x86_apic_borrow(owner->Window, owner->Authority.Data.Base + page * 4096, 0, 1, zero_private, &request) ||
            !x86_page_window_idle_valid(owner->Window)
        )
        return 0;

    for (request.Index = 0; request.Index < 3; ++request.Index) {
        const KERNEL_PHYSICAL_LEASE *lease = &owner->Authority.Tables[request.Index];

        if (!unreferenced_private(owner->Inventory, lease->Base, 1) ||
            !kernel_physical_is_pinned(owner->Window->Pool, lease) ||
            !x86_apic_borrow(owner->Window, lease->Base, 0, 1, write_private, &request) ||
            !x86_page_window_idle_valid(owner->Window)
        )
        return 0;
    }

    return 1;
}

// ====== x86_dma_arena_unbind ======

// Reverse of bind: unpublish the slot and flush, unregister, capture, then free tables and data.
int x86_dma_arena_unbind(X86_DMA_ARENA *owner)
{
    X86_FB_CPU cpu;

    if (!owner || owner->Self != (BOOT_U64)owner || !owner->Window || !owner->Window->Pool || owner->Allocated > 3 ||
        (owner->Pinned >> owner->Allocated) || owner->Published > 1 || owner->Registered > 1 ||
        !x86_fb_cpu(&cpu) || cpu.Root != owner->Authority.Root || cpu.Pat != owner->Authority.Pat ||
        !x86_page_window_idle_valid(owner->Window)
    )
    return 0;

    volatile BOOT_U64 *root = x86_fb_root();
    KERNEL_PHYSICAL_ALLOCATOR *pool = owner->Window->Pool;

    if (!root) return 0;

    if (owner->Published) {
        if (root[X86_DMA_SLOT] && (root[X86_DMA_SLOT] & ~0x60ULL) != (owner->Authority.Tables[0].Base | 3)) return 0;

        root[X86_DMA_SLOT] = 0;

        if (!x86_fb_flush(cpu.Root) || root[X86_DMA_SLOT]) return 0;

        owner->Published = 0;
    }
    else if (root[X86_DMA_SLOT]) return 0;

    if (owner->Registered) {
        if (!x86_dma_register(&owner->Authority, 0, 0)) return 0;

        owner->Registered = 0;
    }
    else if (x86_dma_owner() == &owner->Authority) return 0;

    owner->Authority.Ready = 0;

    if (!owner->Capture(owner->Context, pool, owner->Lapic, owner->Inventory)) return 0;

    for (BOOT_U32 index = owner->Allocated; index; --index) {
        KERNEL_PHYSICAL_LEASE *lease = &owner->Authority.Tables[index - 1];

        if (owner->Pinned & (1U << (index - 1))) {
            if (!kernel_physical_pin(pool, lease, 0)) return 0;

            owner->Pinned &= ~(1U << (index - 1));
        }

        if (!kernel_physical_release(pool, lease)) return 0;

        --owner->Allocated;
    }

    if (owner->DataPinned) {
        if (!kernel_physical_pin(pool, &owner->Authority.Data, 0)) return 0;

        owner->DataPinned = 0;
    }

    if (owner->DataAllocated) {
        if (!kernel_physical_release(pool, &owner->Authority.Data)) return 0;

        owner->DataAllocated = 0;
    }

    if (!kernel_physical_budget_matches(pool, owner->Budget)) return 0;

    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;

    return 1;
}

// ====== inputs_private ======

static int inputs_private(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, X86_PAGING_INVENTORY *inventory,
    X86_DMA_CAPTURE capture, BOOT_U32 pages, const X86_DMA_ARENA *owner)
{
    BOOT_U64 self = (BOOT_U64)owner, bytes = sizeof(*owner);

    if (!context || !window || !window->Pool || !inventory || !capture || !owner || (self & 7) ||
        !pages || pages > X86_DMA_PAGES || x86_dma_owner() ||
        os_address_ranges_overlap(self, bytes, (BOOT_U64)context, sizeof(*context)) ||
        os_address_ranges_overlap(self, bytes, (BOOT_U64)window, sizeof(*window)) ||
        os_address_ranges_overlap(self, bytes, (BOOT_U64)window->Pool, sizeof(*window->Pool)) ||
        os_address_ranges_overlap(self, bytes, (BOOT_U64)inventory, sizeof(*inventory)) ||
        os_address_ranges_overlap(self, bytes, X86_DMA_VIRTUAL - 4096, (X86_DMA_PAGES + 2) * 4096ULL) ||
        os_address_ranges_overlap(self, bytes, X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES) ||
        !x86_page_window_idle_valid(window)
    )
    return 0;

    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;

    return 1;
}

// ====== x86_dma_arena_bind ======

int x86_dma_arena_bind(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory, X86_DMA_CAPTURE capture, BOOT_U32 pages, X86_DMA_ARENA *owner)
{
    X86_FB_CPU cpu;

    if (!inputs_private(context, window, inventory, capture, pages, owner) || !x86_fb_cpu(&cpu) || (cpu.Pat & 255) != 6 ||
        cpu.Root != context->Info.PageTableRoot || !x86_fb_root() || x86_fb_root()[X86_DMA_SLOT] ||
        !capture(context, window->Pool, lapic, inventory) || !kernel_physical_budget_at_least(window->Pool, pages + 3)
    )
    return 0;

    KERNEL_PHYSICAL_ALLOCATOR *pool = window->Pool;

    owner->Self = (BOOT_U64)owner; if (!kernel_physical_budget_read(pool, &owner->Budget)) return 0;
    owner->Context = context; owner->Window = window; owner->Lapic = lapic; owner->Inventory = inventory; owner->Capture = capture;
    owner->Authority.Self = (BOOT_U64)&owner->Authority; owner->Authority.Root = cpu.Root;
    owner->Authority.Pat = cpu.Pat; owner->Authority.PhysicalBits = cpu.PhysicalBits;

    if (!kernel_physical_allocate(pool, pages, &owner->Authority.Data)) goto rollback;

    owner->DataAllocated = 1;

    if (!kernel_physical_pin(pool, &owner->Authority.Data, 1)) goto rollback;

    owner->DataPinned = 1;

    for (BOOT_U32 index = 0; index < 3; ++index) {
        if (!kernel_physical_allocate(pool, 1, &owner->Authority.Tables[index])) goto rollback;

        ++owner->Allocated;

        if (!kernel_physical_pin(pool, &owner->Authority.Tables[index], 1)) goto rollback;

        owner->Pinned |= 1U << index;
    }

    if (!construct_private(owner)) goto rollback;

    owner->Published = 1;
    x86_fb_root()[X86_DMA_SLOT] = owner->Authority.Tables[0].Base | 3;

    if (!x86_fb_flush(cpu.Root)) goto rollback;

    owner->Authority.Ready = 1;

    if (!x86_dma_register(0, &owner->Authority, x86_dma_arena_check)) goto rollback;

    owner->Registered = 1;

    if (!capture(context, pool, lapic, inventory)) goto rollback;

    return 1;

rollback:
    (void)x86_dma_arena_unbind(owner);
    return 0;
}

// ====== x86_dma_arena_bytes ======

volatile unsigned char *x86_dma_arena_bytes(X86_DMA_ARENA *owner)
{
    if (!owner || owner->Self != (BOOT_U64)owner || x86_dma_owner() != &owner->Authority ||
        !x86_dma_arena_check(&owner->Authority) ||
        !owner->Capture(owner->Context, owner->Window->Pool, owner->Lapic, owner->Inventory)
    )
    return 0;

    return (volatile unsigned char *)X86_DMA_VIRTUAL;
}

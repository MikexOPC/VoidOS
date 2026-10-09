#include "transaction/internal.h"
#include "arch/x86/mm/window/access.h"
#include "lib/memory/address_ranges.h"
typedef struct {
    X86_FB_MAPPING  *Owner;
    BOOT_U32  Table, Reserved;
} TABLE_WRITE;

// ====== detached_aliases_absent ======

static int detached_aliases_absent(const X86_FB_MAPPING *owner)
{
    BOOT_U32 lease, table, slot;
    for (lease = 0; lease < owner->Allocated; ++lease) {
        BOOT_U64 physical = owner->Authority.Tables[lease].Base;
        for (table = 0; table < owner->Inventory->Count; ++table) {
            const X86_PAGING_RESOURCE *record = &owner->Inventory->Tables[table];
            const volatile BOOT_U64 *entries;
            if (record->Physical == physical) return 0;
            if (record->Level != 3) continue;
            entries = x86_paging_view(record->View);
            if (!entries) return 0;
            for (slot = 0; slot < 512; ++slot)
                if (entries[slot] && (entries[slot] & 0x000ffffffffff000ULL) == physical) return 0;
        }
    }
    return 1;
}

// ====== write_detached ======

static int write_detached(void *opaque)
{
    TABLE_WRITE *request = (TABLE_WRITE *)opaque;
    X86_PAGING_VIDEO_AUTH *auth = &request->Owner->Authority;
    volatile BOOT_U64 *page = (volatile BOOT_U64 *)x86_window_bytes();
    BOOT_U32 slot;
    if (!page) return 0;
    for (slot = 0; slot < 512; ++slot) {
        BOOT_U64 child = 0, entry;
        if (!request->Table && !slot) child = auth->Tables[1].Base;
        if (request->Table == 1 && slot < auth->Region.TableCount - 2) child = auth->Tables[slot + 2].Base;
        entry = x86_fb_table_entry(request->Table, slot, auth->Region.Physical,
            auth->Region.Pages, auth->Region.Cache, auth->PhysicalBits, child);
        if (entry == ~0ULL) return 0;
        page[slot] = entry;
        if (page[slot] != entry) return 0;
    }
    return 1;
}

// ====== x86_fb_construct ======

int x86_fb_construct(X86_FB_MAPPING *owner)
{
    TABLE_WRITE request = {owner, 0, 0};
    if (!detached_aliases_absent(owner)) return 0;
    for (request.Table = 0; request.Table < owner->Allocated; ++request.Table) {
        const KERNEL_PHYSICAL_LEASE *lease = &owner->Authority.Tables[request.Table];
        if (!kernel_physical_is_pinned(owner->Window->Pool, lease) ||
            !x86_apic_borrow(owner->Window, lease->Base, 0, 1, write_detached, &request) ||
            !x86_page_window_idle_valid(owner->Window)) return 0;
    }
    return 1;
}

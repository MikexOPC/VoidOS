#include "resources.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
typedef struct { X86_AP_RESOURCES *Owner; BOOT_U32 Index, Data; } AP_WRITE;

// ====== entry_private ======

static BOOT_U64 entry_private(const X86_AP_AUTH *auth, BOOT_U32 table, BOOT_U32 slot) {
    // Intel SDM 078US 4.5/4.6: P bit0=1, RW bit1=1, U/S bit2=0; leaf XD bit63=1. PAT0 is validated WB.
    if (!table) return slot ? 0 : (auth->Tables.Base + 4096) | 3;
    if (table == 1) return slot && slot <= auth->Tables.Pages - 2 ? (auth->Tables.Base + (slot + 1ULL) * 4096) | 3 : 0;
    BOOT_U64 page = (table - 2ULL) * 512 + slot;
    BOOT_U32 index = xa_geometry_page(auth->Cpus, page);
    if (index != x86_ap_page(auth->Cpus, page)) return UINT64_MAX;
    return index == UINT32_MAX ? 0 : (auth->Data.Base + index * 4096ULL) | X86_AP_NX | 3;
}

// ====== write_private ======

static int write_private(void *opaque) {
    AP_WRITE *request = opaque;
    volatile BOOT_U64 *view = (volatile BOOT_U64 *)x86_window_bytes();
    if (!view) return 0;
    for (BOOT_U32 slot = 0; slot < 512; ++slot) {
        BOOT_U64 value = request->Data ? 0 : entry_private(&request->Owner->Authority, request->Index, slot);
        if (value == UINT64_MAX) return 0;
        view[slot] = value;
        if (view[slot] != value) return 0;
    }
    if (request->Data && request->Index % 29 == 28) {
        BOOT_U32 cpu = request->Index / 29 + 1;
        return x86_ap_descriptor_prepare((X86_KERNEL_SEGMENTS *)view, cpu) &&
            x86_ap_descriptor_verify((const X86_KERNEL_SEGMENTS *)view, cpu);
    }
    return 1;
}

// ====== unreferenced_private ======

static int unreferenced_private(const X86_PAGING_INVENTORY *inventory, BOOT_U64 base, BOOT_U64 pages) {
    for (BOOT_U32 index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        if (kernel_regions_overlap(record->Physical, 1, base, pages)) return 0;
        if (record->Level != 3) continue;
        const volatile BOOT_U64 *entries = x86_paging_view(record->View);
        if (!entries) return 0;
        for (BOOT_U32 slot = 0; slot < 512; ++slot)
            if (entries[slot] && kernel_regions_overlap(entries[slot] & X86_AP_ADDRESS, 1, base, pages)) return 0;
    }
    return 1;
}

// ====== x86_ap_construct ======

int x86_ap_construct(X86_AP_RESOURCES *owner) {
    AP_WRITE request = {owner, 0, 1};
    if (xa_geometry_tables(owner->Authority.Cpus) != owner->Authority.Tables.Pages ||
        !unreferenced_private(owner->Inventory, owner->Authority.Data.Base, owner->Authority.Data.Pages) ||
        !unreferenced_private(owner->Inventory, owner->Authority.Tables.Base, owner->Authority.Tables.Pages)) return 0;
    for (request.Data = 1; request.Data <= 2; ++request.Data) {
        BOOT_U32 data = request.Data == 1;
        const KERNEL_PHYSICAL_LEASE *lease = data ? &owner->Authority.Data : &owner->Authority.Tables;
        for (request.Index = 0; request.Index < lease->Pages; ++request.Index) {
            AP_WRITE item = {owner, request.Index, data};
            if (!x86_apic_borrow(owner->Window, lease->Base + request.Index * 4096ULL, 0, 1, write_private, &item) ||
                !x86_page_window_idle_valid(owner->Window)) return 0;
        }
    }
    return 1;
}

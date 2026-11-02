#include "authority.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"
static const X86_AP_AUTH *registered;
static X86_AP_CHECK registered_check;

// ====== x86_ap_owner ======

const X86_AP_AUTH *x86_ap_owner(void) { return registered; }

// ====== auth_valid ======

static int auth_valid(const X86_AP_AUTH *owner)
{
    return owner && owner->Self == (BOOT_U64)owner && owner->Ready == 1 && !owner->Reserved && owner->Cpus >= 2 && owner->Cpus <= 64 &&
        owner->Data.Pages == (owner->Cpus - 1U) * 29U && owner->Tables.Pages == x86_ap_tables(owner->Cpus) && (owner->Pat & 255) == 6 &&
        owner->PhysicalBits >= 32 && owner->PhysicalBits <= 52 &&
        !kernel_regions_overlap(owner->Tables.Base, x86_ap_tables(owner->Cpus), owner->Data.Base, (owner->Cpus - 1U) * 29U);
}

// ====== x86_ap_register ======

int x86_ap_register(const X86_AP_AUTH *expected, const X86_AP_AUTH *next, X86_AP_CHECK check)
{
    if (registered != expected || (!next && check) ||
        (next && (expected || !check || !auth_valid(next) || !check(next)))
    )
    return 0;

    registered_check = check;
    registered = next;
    return 1;
}

// ====== x86_ap_inputs ======

int x86_ap_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U64 output, BOOT_U64 bytes,
    X86_AP_SNAPSHOT *snapshot)
{
    const X86_AP_AUTH *owner = registered;

    if (!snapshot) return 0;

    *snapshot = (X86_AP_SNAPSHOT){0};

    if (!owner) return 1;

    if (!pool || !auth_valid(owner) || owner->Root != root || owner->PhysicalBits != pool->PhysicalBits ||
        !kernel_physical_is_pinned(pool, &owner->Data) || !kernel_physical_is_pinned(pool, &owner->Tables) ||
        !registered_check || !registered_check(owner) ||
        os_address_ranges_overlap(output, bytes, (BOOT_U64)owner, sizeof(*owner)) ||
        os_address_ranges_overlap(output, bytes, X86_AP_BASE, X86_AP_REGION_BYTES)
    )
    return 0;

    *snapshot = (X86_AP_SNAPSHOT){owner->Self, owner->Data.Base, owner->Data.Pages, owner->Data.Id,
        owner->Tables.Base, owner->Tables.Id, owner->Cpus, (BOOT_U32)owner->Tables.Pages};
    return 1;
}

// ====== x86_ap_source ======

BOOT_U32 x86_ap_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    const X86_AP_AUTH *owner = registered;

    if (!owner || physical < owner->Tables.Base || physical - owner->Tables.Base >= x86_ap_tables(owner->Cpus) * 4096ULL ||
        (physical & 4095)
    )
    return 0;

    return kernel_physical_is_pinned(pool, &owner->Tables) ?
        X86_AP_TAG + 1 + (BOOT_U32)((physical - owner->Tables.Base) / 4096) : 0;
}

// ====== x86_ap_leaf ======

// Intel SDM 078US 4.5/4.6: P bit0=1, RW bit1=1, U/S bit2=0, XD bit63=1; WB via PAT0, G bit8=0.
// Returns 10 for owned AP RAM, 0 for wrong entries/aliases, 1 when unrelated.
BOOT_U32 x86_ap_leaf(BOOT_U64 entry, BOOT_U64 address)
{
    const X86_AP_AUTH *owner = registered;

    if (!owner) return X86_LAYOUT_SLOT_OF(address) == X86_AP_SLOT ? 0 : 1;

    if (X86_LAYOUT_SLOT_OF(address) == X86_AP_SLOT) {
        if (address < X86_AP_BASE || (address & 4095)) return 0;
        BOOT_U32 index = x86_ap_page(owner->Cpus, (address - X86_AP_BASE) / 4096);
        return index != UINT32_MAX &&
            (entry & ~0x60ULL) == ((owner->Data.Base + index * 4096ULL) | X86_AP_NX | 3) ? 10 : 0;
    }

    BOOT_U64 physical = entry & 0x000ffffffffff000ULL;

    return kernel_regions_overlap(physical, 1, owner->Data.Base, (owner->Cpus - 1U) * 29U) ||
        kernel_regions_overlap(physical, 1, owner->Tables.Base, x86_ap_tables(owner->Cpus)) ? 0 : 1;
}

// ====== x86_ap_record ======

int x86_ap_record(const X86_AP_SNAPSHOT *snapshot, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level, BOOT_U32 tag) {
    if (!snapshot || !snapshot->Owner || snapshot->Cpus < 2 || snapshot->Cpus > 64 ||
        snapshot->Tables != x86_ap_tables(snapshot->Cpus) || tag < X86_AP_TAG || tag - X86_AP_TAG >= snapshot->Tables) return 0;
    BOOT_U32 index = tag - X86_AP_TAG;
    BOOT_U64 expected = index == 0 ? X86_LAYOUT_RECURSIVE_VIEW(510,510,510,X86_AP_SLOT) :
        index == 1 ? X86_LAYOUT_RECURSIVE_VIEW(510,510,X86_AP_SLOT,0) :
        X86_LAYOUT_RECURSIVE_VIEW(510,X86_AP_SLOT,0,index - 1);
    return physical == snapshot->TableBase + index * 4096ULL && view == expected &&
        level == (index < 2 ? index + 1 : 3);
}

// ====== x86_ap_complete ======

int x86_ap_complete(const X86_AP_SNAPSHOT *snapshot, BOOT_U32 seen, BOOT_U64 leaves) {
    if (!snapshot) return 0;
    if (!registered) {
        const unsigned char *bytes = (const unsigned char *)snapshot;
        for (BOOT_U32 index = 0; index < sizeof(*snapshot); ++index) if (bytes[index]) return 0;
        return !seen && !leaves;
    }
    const X86_AP_AUTH *owner = registered;
    return auth_valid(owner) && snapshot->Owner == owner->Self && snapshot->Base == owner->Data.Base &&
        snapshot->Pages == owner->Data.Pages && snapshot->DataId == owner->Data.Id &&
        snapshot->TableBase == owner->Tables.Base && snapshot->TableId == owner->Tables.Id &&
        snapshot->Cpus == owner->Cpus && snapshot->Tables == owner->Tables.Pages &&
        seen == (1U << snapshot->Tables) - 1 && leaves == snapshot->Pages && registered_check && registered_check(owner);
}

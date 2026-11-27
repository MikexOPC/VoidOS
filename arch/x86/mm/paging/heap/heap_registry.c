#include "authority.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"
static const X86_HEAP_AUTH *registered;
static X86_HEAP_CHECK registered_check;

// ====== x86_heap_owner ======

const X86_HEAP_AUTH *x86_heap_owner(void) { return registered; }

// ====== auth_valid ======

static int auth_valid(const X86_HEAP_AUTH *owner)
{
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 1)
        return 0;
    BOOT_U64 data_pts = owner->Data.Pages / 512ULL;
    if ((owner->Data.Pages % 512ULL) != 0ULL || data_pts < X86_HEAP_MIN_DATA_PTS || data_pts > X86_HEAP_MAX_DATA_PTS)
        return 0;
    BOOT_U64 expected_tables = 2ULL + data_pts + X86_HEAP_STACK_PTS;
    return owner->Tables.Pages == expected_tables && (owner->Pat & 255) == 6 &&
        owner->PhysicalBits >= 32 && owner->PhysicalBits <= 52 &&
        !kernel_regions_overlap(owner->Tables.Base, owner->Tables.Pages, owner->Data.Base, owner->Data.Pages);
}

// ====== x86_heap_register ======

int x86_heap_register(const X86_HEAP_AUTH *expected, const X86_HEAP_AUTH *next, X86_HEAP_CHECK check)
{
    if (registered != expected || (!next && check) ||
        (next && (expected || !check || !auth_valid(next) || !check(next)))
    )
    return 0;

    registered_check = check;
    registered = next;
    return 1;
}

// ====== x86_heap_inputs ======

int x86_heap_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U64 output, BOOT_U64 bytes,
    X86_HEAP_SNAPSHOT *snapshot)
{
    const X86_HEAP_AUTH *owner = registered;

    if (!snapshot) return 0;

    *snapshot = (X86_HEAP_SNAPSHOT){0};

    if (!owner) return 1;

    if (!pool || !auth_valid(owner) || owner->Root != root || owner->PhysicalBits != pool->PhysicalBits ||
        !kernel_physical_is_pinned(pool, &owner->Data) || !kernel_physical_is_pinned(pool, &owner->Tables) ||
        !registered_check || !registered_check(owner) ||
        os_address_ranges_overlap(output, bytes, (BOOT_U64)owner, sizeof(*owner)) ||
        os_address_ranges_overlap(output, bytes, X86_HEAP_VIRTUAL, owner->Data.Pages * 4096ULL)
    )
    return 0;

    *snapshot = (X86_HEAP_SNAPSHOT){owner->Self, owner->Data.Base, owner->Data.Pages, owner->Data.Id,
        owner->Tables.Base, owner->Tables.Id};
    return 1;
}

// ====== x86_heap_source ======

BOOT_U32 x86_heap_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    const X86_HEAP_AUTH *owner = registered;

    if (!owner || physical < owner->Tables.Base || physical - owner->Tables.Base >= owner->Tables.Pages * 4096ULL ||
        (physical & 4095)
    )
    return 0;

    return kernel_physical_is_pinned(pool, &owner->Tables) ?
        X86_HEAP_TAG + 1 + (BOOT_U32)((physical - owner->Tables.Base) / 4096) : 0;
}

// ====== x86_heap_leaf ======

// Intel SDM Vol. 3A 5.5.4/5.6: P0, RW1, US2=0, XD63; WB through PAT entry 0, no global bit.
// Returns 9 for a heap leaf, 0 for a wrong entry or an alias of the heap, 1 when unrelated.
BOOT_U32 x86_heap_leaf(BOOT_U64 entry, BOOT_U64 address)
{
    const X86_HEAP_AUTH *owner = registered;

    if (!owner) return 1;

    if (X86_LAYOUT_SLOT_OF(address) == X86_HEAP_SLOT) {
        if (address & 4095) return 0;

        if (address >= X86_HEAP_VIRTUAL && address < X86_HEAP_VIRTUAL + owner->Data.Pages * 4096ULL) {
            BOOT_U64 index = (address - X86_HEAP_VIRTUAL) / 4096;

            return (entry & ~0x60ULL) == ((owner->Data.Base + index * 4096) | 0x8000000000000003ULL) ? 9 : 0;
        }

        if (address >= X86_HEAP_STACK_BASE &&
            address < X86_HEAP_STACK_BASE + (BOOT_U64)X86_HEAP_STACK_PTS * 512 * 4096ULL) {
            BOOT_U64 physical = entry & 0x000ffffffffff000ULL;

            return kernel_regions_overlap(physical, 1, owner->Data.Base, owner->Data.Pages) ||
                kernel_regions_overlap(physical, 1, owner->Tables.Base, owner->Tables.Pages) ? 0 : 1;
        }

        return 0;
    }

    BOOT_U64 physical = entry & 0x000ffffffffff000ULL;

    return kernel_regions_overlap(physical, 1, owner->Data.Base, owner->Data.Pages) ||
        kernel_regions_overlap(physical, 1, owner->Tables.Base, owner->Tables.Pages) ? 0 : 1;
}

// ====== x86_heap_record ======

int x86_heap_record(const X86_HEAP_SNAPSHOT *snapshot, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level, BOOT_U32 tag)
{
    if (!registered || !snapshot || snapshot->Owner != registered->Self || tag < X86_HEAP_TAG ||
        tag >= X86_HEAP_TAG + registered->Tables.Pages || level < 1 || level > 3
    )
    return 0;

    BOOT_U32 index = tag - X86_HEAP_TAG;

    return level == X86_HEAP_LEVEL(index) && physical == registered->Tables.Base + index * 4096ULL &&
        snapshot->TableId == registered->Tables.Id &&
        ((view >> (12 + (level - 1) * 9)) & 511) == X86_HEAP_SLOT;
}

// ====== x86_heap_complete ======

int x86_heap_complete(const X86_HEAP_SNAPSHOT *snapshot, BOOT_U64 seen, BOOT_U64 leaves)
{
    if (!snapshot) return 0;

    if (!registered) {
        for (BOOT_U32 byte = 0; byte < sizeof(*snapshot); ++byte) if (((const unsigned char *)snapshot)[byte]) return 0;

        return !seen && !leaves;
    }

    return auth_valid(registered) && snapshot->Owner == registered->Self && snapshot->Base == registered->Data.Base &&
        snapshot->DataId == registered->Data.Id && snapshot->Pages == registered->Data.Pages &&
        snapshot->TableBase == registered->Tables.Base && snapshot->TableId == registered->Tables.Id &&
        seen == ((1ULL << registered->Tables.Pages) - 1ULL) && leaves == registered->Data.Pages &&
        registered_check && registered_check(registered);
}

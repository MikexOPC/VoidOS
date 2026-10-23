#include "authority.h"
#include "lib/memory/address_ranges.h"
static const X86_DMA_AUTH *registered;
static X86_DMA_CHECK registered_check;

// Inventory tags: Reserved 40-42 (table source 41-43) for the PDPT, PD and PT of the arena.
#define DMA_TAG 40U

// ====== x86_dma_owner ======

const X86_DMA_AUTH *x86_dma_owner(void) { return registered; }

// ====== auth_valid ======

static int auth_valid(const X86_DMA_AUTH *owner)
{
    return owner && owner->Self == (BOOT_U64)owner && owner->Ready == 1 &&
        owner->Data.Pages && owner->Data.Pages <= X86_DMA_PAGES && (owner->Pat & 255) == 6 &&
        owner->PhysicalBits >= 32 && owner->PhysicalBits <= 52;
}

// ====== x86_dma_register ======

int x86_dma_register(const X86_DMA_AUTH *expected, const X86_DMA_AUTH *next, X86_DMA_CHECK check)
{
    if (registered != expected || (!next && check) ||
        (next && (expected || !check || !auth_valid(next) || !check(next)))
    )
    return 0;

    registered_check = check;
    registered = next;
    return 1;
}

// ====== x86_dma_inputs ======

int x86_dma_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U64 output, BOOT_U64 bytes,
    X86_DMA_SNAPSHOT *snapshot)
{
    const X86_DMA_AUTH *owner = registered;

    if (!snapshot) return 0;

    *snapshot = (X86_DMA_SNAPSHOT){0};

    if (!owner) return 1;

    if (!pool || !auth_valid(owner) || owner->Root != root || owner->PhysicalBits != pool->PhysicalBits ||
        !kernel_physical_is_pinned(pool, &owner->Data) || !registered_check || !registered_check(owner) ||
        os_address_ranges_overlap(output, bytes, (BOOT_U64)owner, sizeof(*owner)) ||
        os_address_ranges_overlap(output, bytes, X86_DMA_VIRTUAL - 4096, (X86_DMA_PAGES + 2) * 4096ULL)
    )
    return 0;

    snapshot->Owner = owner->Self;
    snapshot->Base = owner->Data.Base;
    snapshot->Pages = owner->Data.Pages;
    snapshot->DataId = owner->Data.Id;

    for (BOOT_U32 index = 0; index < 3; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease = &owner->Tables[index];

        if (lease->Pages != 1 || !kernel_physical_is_pinned(pool, lease) ||
            kernel_regions_overlap(lease->Base, 1, owner->Data.Base, owner->Data.Pages)
        )
        return 0;

        for (BOOT_U32 other = 0; other < index; ++other)
            if (lease->Base == owner->Tables[other].Base || lease->Id == owner->Tables[other].Id) return 0;

        snapshot->TableIds[index] = lease->Id;
    }

    return 1;
}

// ====== x86_dma_source ======

BOOT_U32 x86_dma_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    if (registered)
        for (BOOT_U32 index = 0; index < 3; ++index)
            if (registered->Tables[index].Base == physical)
                return kernel_physical_is_pinned(pool, &registered->Tables[index]) ? DMA_TAG + 1 + index : 0;

    return 0;
}

// ====== x86_dma_leaf ======

// Intel SDM Vol. 3A 5.5.4/5.6: P0, RW1, US2=0, XD63; WB through PAT entry 0, no global bit.
// Returns 8 for an arena leaf, 0 for a wrong entry or an alias of the arena, 1 when unrelated.
BOOT_U32 x86_dma_leaf(BOOT_U64 entry, BOOT_U64 address)
{
    const X86_DMA_AUTH *owner = registered;

    if (!owner) return 1;

    if ((address >> 39) == X86_DMA_SLOT) {
        if (address < X86_DMA_VIRTUAL || (address & 4095)) return 0;

        BOOT_U64 index = (address - X86_DMA_VIRTUAL) / 4096;

        return index < owner->Data.Pages &&
            (entry & ~0x60ULL) == ((owner->Data.Base + index * 4096) | 0x8000000000000003ULL) ? 8 : 0;
    }

    return kernel_regions_overlap(entry & 0x000ffffffffff000ULL, 1, owner->Data.Base, owner->Data.Pages) ? 0 : 1;
}

// ====== x86_dma_record ======

int x86_dma_record(const X86_DMA_SNAPSHOT *snapshot, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level, BOOT_U32 tag)
{
    if (!registered || !snapshot || snapshot->Owner != registered->Self || tag < DMA_TAG || tag > DMA_TAG + 2 ||
        level < 1 || level > 3
    )
    return 0;

    BOOT_U32 index = tag - DMA_TAG;

    return level == index + 1 && physical == registered->Tables[index].Base &&
        snapshot->TableIds[index] == registered->Tables[index].Id &&
        ((view >> (12 + (level - 1) * 9)) & 511) == X86_DMA_SLOT;
}

// ====== x86_dma_complete ======

int x86_dma_complete(const X86_DMA_SNAPSHOT *snapshot, BOOT_U32 seen, BOOT_U64 leaves)
{
    if (!snapshot) return 0;

    if (!registered) {
        for (BOOT_U32 byte = 0; byte < sizeof(*snapshot); ++byte) if (((const unsigned char *)snapshot)[byte]) return 0;

        return !seen && !leaves;
    }

    if (!auth_valid(registered) || snapshot->Owner != registered->Self || snapshot->Base != registered->Data.Base ||
        snapshot->DataId != registered->Data.Id || snapshot->Pages != registered->Data.Pages ||
        seen != 7 || leaves != registered->Data.Pages || !registered_check || !registered_check(registered)
    )
    return 0;

    for (BOOT_U32 index = 0; index < 3; ++index)
        if (snapshot->TableIds[index] != registered->Tables[index].Id) return 0;

    return 1;
}

#include "inventory.h"
#include "lib/memory/address_ranges.h"
#define PAGING_ADDRESS 0x000ffffffffff000ULL
#define PAGING_NX (1ULL << 63)
#define PAGING_ROOT_VIEW ((128ULL << 39) | (128ULL << 30) | (128ULL << 21) | (128ULL << 12))

// ====== fotografia UEFI coerente con il totale conservato dall'allocatore ===== //

static int map_consistent(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool)
{
    KERNEL_MEMORY_INVENTORY inventory;
    KERNEL_MEMORY_REGION region;
    BOOT_U64 index, total = 0;
    if (!kernel_inventory_memory(context->MemoryMap, context->Info.MemoryMapBytes,
                                  context->Info.MemoryDescriptorBytes, &inventory)) return 0;
    for (index = 0; index < inventory.Regions; ++index) {
        BOOT_U64 pages;
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
                                context->Info.MemoryDescriptorBytes, index, &region)) return 0;
        pages = kernel_physical_pages(region.Base, region.Pages, region.Attributes, region.Type, pool->PhysicalBits);
        if (pages > ~0ULL - total) return 0;
        total += pages;
    }
    return total == pool->TotalPages;
}

// ====== buffer kernel leggibili disgiunti e output fuori dalla vista ricorsiva ===== //

static int buffers_valid(const KERNEL_BOOT_CONTEXT *context,
                         const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *output)
{
    BOOT_U64 map, bytes, stride, target = (BOOT_U64)output;
    if (!context || !pool || !output || ((BOOT_U64)context & 7) ||
        ((BOOT_U64)pool & 7) || (target & 7) || target > ~0ULL - sizeof(*output)) return 0;
    map = (BOOT_U64)context->MemoryMap; bytes = context->Info.MemoryMapBytes;
    stride = context->Info.MemoryDescriptorBytes;
    if (!map || !bytes || bytes > 1048576 || stride < 40 || stride > 4096 ||
        bytes % stride || bytes / stride > 4096 || map > ~0ULL - bytes) return 0;
    if (os_address_ranges_overlap(target, sizeof(*output), map, bytes) ||
        os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)context, sizeof(*context)) ||
        os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)pool, sizeof(*pool)) ||
        os_address_ranges_overlap(target, sizeof(*output), 128ULL << 39, 1ULL << 39)) return 0;
    return kernel_physical_validate(pool) && map_consistent(context, pool);
}

// ====== tabella ereditata interamente dentro EfiLoaderData ordinaria WB ===== //

static int table_reserved(const KERNEL_BOOT_CONTEXT *context,
                           const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    KERNEL_MEMORY_REGION region;
    BOOT_U64 index, matches = 0;
    if (!x86_paging_entry_valid(physical | 3, 0, pool->PhysicalBits)) return 0;
    for (index = 0; index < context->Info.MemoryMapBytes / context->Info.MemoryDescriptorBytes; ++index) {
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
                                context->Info.MemoryDescriptorBytes, index, &region) ||
            !kernel_region_valid(region.Base, region.Pages, region.Attributes, region.Type)) return 0;
        if (!kernel_regions_overlap(physical, 1, region.Base, region.Pages)) continue;
        if (region.Type != 2 || !(region.Attributes & 8) || (region.Attributes & ~0x1fULL) ||
            physical < region.Base || physical - region.Base > (region.Pages - 1) * 4096) return 0;
        ++matches;
    }
    if (matches != 1) return 0;
    for (index = 0; index < pool->Count; ++index)
        if (kernel_regions_overlap(physical, 1, pool->Free[index].Base, pool->Free[index].Pages)) return 0;
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
        if (pool->Live[index].Id && kernel_regions_overlap(physical, 1, pool->Live[index].Base, pool->Live[index].Pages)) return 0;
    return 1;
}

// ====== registrazione bounded prima di ogni accesso al figlio ===== //

static int append_table(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
                         X86_PAGING_INVENTORY *draft, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level)
{
    BOOT_U32 index;
    X86_PAGING_RESOURCE *record;
    if (!view || draft->Count == X86_BOOT_TABLE_LIMIT || !table_reserved(context, pool, physical)) return 0;
    for (index = 0; index < draft->Count; ++index)
        if (draft->Tables[index].Physical == physical || draft->Tables[index].View == view) return 0;
    record = &draft->Tables[draft->Count++];
    record->Physical = physical; record->View = view; record->Level = level; record->Reserved = 0;
    return 1;
}

// ====== nessuna foglia ordinaria puo esporre una pagina di paging come dati ===== //

static int leaf_aliases_absent(const X86_PAGING_INVENTORY *draft)
{
    BOOT_U32 index, slot, other;
    for (index = 0; index < draft->Count; ++index) {
        const volatile BOOT_U64 *entries;
        if (draft->Tables[index].Level != 3) continue;
        entries = x86_paging_view(draft->Tables[index].View);
        if (!entries) return 0;
        for (slot = 0; slot < 512; ++slot) {
            BOOT_U64 entry = entries[slot];
            if (!entry) continue;
            for (other = 0; other < draft->Count; ++other)
                if ((entry & PAGING_ADDRESS) == draft->Tables[other].Physical) return 0;
        }
    }
    return 1;
}

// ====== visita iterativa senza seguire il ciclo intenzionale del PML4 ===== //

static int scan(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
                 X86_PAGING_INVENTORY *draft)
{
    BOOT_U32 index, slot;
    for (index = 0; index < sizeof(*draft); ++index) ((volatile unsigned char *)draft)[index] = 0;
    draft->Root = context->Info.PageTableRoot;
    if ((draft->Root & 4095) || !append_table(context, pool, draft, draft->Root, PAGING_ROOT_VIEW, 0)) return 0;
    for (index = 0; index < draft->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &draft->Tables[index];
        const volatile BOOT_U64 *entries = x86_paging_view(record->View);
        if (!entries) return 0;
        for (slot = 0; slot < 512; ++slot) {
            BOOT_U64 entry = entries[slot];
            if (!record->Level && slot == X86_BOOT_RECURSIVE_SLOT) {
                if ((entry & ~0x60ULL) != (draft->Root | 3 | PAGING_NX)) return 0;
                continue;
            }
            if (!x86_paging_entry_valid(entry, record->Level, pool->PhysicalBits)) return 0;
            if (!entry) continue;
            if (record->Level == 3) { ++draft->LeafPages; continue; }
            if (!append_table(context, pool, draft, entry & PAGING_ADDRESS,
                                x86_paging_child_view(record->View, record->Level, slot), record->Level + 1)) return 0;
        }
    }
    if (!leaf_aliases_absent(draft)) return 0;
    draft->Ready = 1;
    return 1;
}

// ====== pubblicazione della fotografia soltanto dopo scansione completa ===== //

int x86_paging_inventory_capture(const KERNEL_BOOT_CONTEXT *context,
                                  const KERNEL_PHYSICAL_ALLOCATOR *pool, X86_PAGING_INVENTORY *output)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 index;
    if (!buffers_valid(context, pool, output) || !scan(context, pool, &draft)) return 0;
    for (index = 0; index < sizeof(draft); ++index)
        ((volatile unsigned char *)output)[index] = ((const unsigned char *)&draft)[index];
    return 1;
}

// ====== confronto della topologia e del numero di foglie senza scrivere PTE ===== //

static int record_linked(const X86_PAGING_INVENTORY *inventory, BOOT_U32 index)
{
    const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
    BOOT_U32 parent, slot = (BOOT_U32)((record->View >> 12) & 511);
    for (parent = 0; parent < index; ++parent) {
        const X86_PAGING_RESOURCE *ancestor = &inventory->Tables[parent];
        if (ancestor->Level + 1 == record->Level &&
            x86_paging_child_view(ancestor->View, ancestor->Level, slot) == record->View) return 1;
    }
    return 0;
}

// ====== controllo compatto dei record prima della scansione completa ===== //

static int snapshot_valid(const X86_PAGING_INVENTORY *inventory, BOOT_U32 bits)
{
    BOOT_U32 index, other;
    if (inventory->Ready != 1 || !inventory->Count || inventory->Count > X86_BOOT_TABLE_LIMIT ||
        inventory->LeafPages > (BOOT_U64)inventory->Count * 512 ||
        inventory->Tables[0].Physical != inventory->Root ||
        inventory->Tables[0].View != PAGING_ROOT_VIEW || inventory->Tables[0].Level) return 0;
    for (index = 0; index < X86_BOOT_TABLE_LIMIT; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        if (index >= inventory->Count) {
            if (record->Physical || record->View || record->Level || record->Reserved) return 0;
            continue;
        }
        if (record->Reserved || record->Level > 3 || (index && !record->Level) ||
            (record->Physical & 4095) || !x86_paging_entry_valid(record->Physical | 3, 0, bits) ||
            (record->View & 4095) || record->View >> 39 != X86_BOOT_RECURSIVE_SLOT) return 0;
        if (index && !record_linked(inventory, index)) return 0;
        for (other = 0; other < index; ++other)
            if (inventory->Tables[other].Physical == record->Physical ||
                inventory->Tables[other].View == record->View) return 0;
    }
    return 1;
}

// ====== rifiuto dei metadati corrotti prima di una nuova visita hardware ===== //

int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *context,
                                 const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *inventory)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 index;
    if (!buffers_valid(context, pool, inventory) || !snapshot_valid(inventory, pool->PhysicalBits) ||
        inventory->Root != context->Info.PageTableRoot || !scan(context, pool, &draft)) return 0;
    for (index = 0; index < sizeof(draft); ++index)
        if (((const unsigned char *)inventory)[index] != ((const unsigned char *)&draft)[index]) return 0;
    return 1;
}

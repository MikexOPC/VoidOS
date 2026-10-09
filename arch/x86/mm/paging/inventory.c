#include "inventory.h"
#include "authorization/validation.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/mm/devices/lapic.h"
#define PAGING_ADDRESS 0x000ffffffffff000ULL
#define PAGING_NX (1ULL << 63)
#define PAGING_ROOT_VIEW ((128ULL << 39) | (128ULL << 30) | (128ULL << 21) | (128ULL << 12))

// ====== registrazione bounded prima di ogni accesso al figlio ===== //

static int append_table(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
                         const X86_PAGING_VIDEO_AUTH *video, X86_PAGING_INVENTORY *draft,
    BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level)
{
    BOOT_U32 index, source;
    X86_PAGING_RESOURCE *record;
    if (!view || draft->Count == X86_BOOT_TABLE_LIMIT) return 0;
    source = x86_paging_table_source(context, pool, video, physical);
    if (!source || (!level && source != 1)) return 0;
    for (index = 0; index < draft->Count; ++index)
        if (draft->Tables[index].Physical == physical || draft->Tables[index].View == view) return 0;
    record = &draft->Tables[draft->Count++];
    record->Physical = physical; record->View = view; record->Level = level; record->Reserved = source - 1;
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
                 const X86_LAPIC_MAPPING *owner, const X86_PAGING_VIDEO_AUTH *video,
    X86_PAGING_INVENTORY *draft)
{
    BOOT_U32 index, slot;
    for (index = 0; index < sizeof(*draft); ++index) ((volatile unsigned char *)draft)[index] = 0;
    draft->Root = context->Info.PageTableRoot;
    if (video) {
        draft->Video = video->Region;
        for (index = 0; index < video->Region.TableCount; ++index)
            draft->VideoTableIds[index] = video->Tables[index].Id;
    }
    if (owner) {
        draft->Device.Virtual = X86_LAPIC_VIRTUAL; draft->Device.Physical = owner->Physical;
        draft->Device.Cache = owner->Cache; draft->Device.Owner = (BOOT_U64)owner;
    }
    if ((draft->Root & 4095) || !append_table(context, pool, video, draft, draft->Root, PAGING_ROOT_VIEW, 0)) return 0;
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
            if (record->Level == 3) {
                BOOT_U32 kind;
                if (!entry) continue;
                BOOT_U64 address = x86_paging_leaf_virtual(record->View,slot);
                kind = video ? x86_paging_video_leaf(entry, address, video->Region.Physical,
                    video->Region.Virtual, video->Region.Pages, video->Region.AperturePages,
                    video->Region.Cache, pool->PhysicalBits) : 1;
                if (kind == 1) kind = x86_paging_leaf_kind(entry,address,
                    draft->Device.Physical,draft->Device.Cache,pool->PhysicalBits);
                if (!kind) return 0;
                if (kind == 2 || kind == 3) ++draft->MmioPages; else ++draft->RamPages;
                ++draft->LeafPages;
                continue;
            }
            if (!x86_paging_entry_valid(entry, record->Level, pool->PhysicalBits)) return 0;
            if (!entry) continue;
            if (!append_table(context, pool, video, draft, entry & PAGING_ADDRESS,
                                x86_paging_child_view(record->View, record->Level, slot), record->Level + 1)) return 0;
        }
    }
    if (!leaf_aliases_absent(draft) || draft->MmioPages != (owner ? 1U : 0U) + (video ? video->Region.Pages : 0)) return 0;
    if (video && !x86_paging_video_tables_complete(video, draft)) return 0;
    draft->Ready = 1;
    return 1;
}

// ====== pubblicazione della fotografia soltanto dopo scansione completa ===== //

int x86_paging_inventory_capture(const KERNEL_BOOT_CONTEXT *context,
                                  const KERNEL_PHYSICAL_ALLOCATOR *pool, X86_PAGING_INVENTORY *output)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 index;
    if (!x86_paging_buffers_valid(context, pool, output) || !scan(context, pool, 0, 0, &draft)) return 0;
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
        inventory->MmioPages > 4322 || inventory->MmioPages > inventory->LeafPages ||
        inventory->RamPages > inventory->LeafPages ||
        inventory->RamPages != inventory->LeafPages-inventory->MmioPages ||
        inventory->Tables[0].Physical != inventory->Root ||
        inventory->Tables[0].View != PAGING_ROOT_VIEW || inventory->Tables[0].Level) return 0;
    for (index = 0; index < X86_BOOT_TABLE_LIMIT; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        if (index >= inventory->Count) {
            if (record->Physical || record->View || record->Level || record->Reserved) return 0;
            continue;
        }
        if (record->Reserved > inventory->Video.TableCount || record->Level > 3 || (index && !record->Level) ||
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
    if (!x86_paging_buffers_valid(context, pool, inventory) || !snapshot_valid(inventory, pool->PhysicalBits) ||
        inventory->Root != context->Info.PageTableRoot || !scan(context, pool, 0, 0, &draft)) return 0;
    for (index = 0; index < sizeof(draft); ++index)
        if (((const unsigned char *)inventory)[index] != ((const unsigned char *)&draft)[index]) return 0;
    return 1;
}

// ====== riserva dispositivo distinta da RAM libera lease e pagine tabelle ===== //

static int device_reserved(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
    const X86_LAPIC_MAPPING *owner)
{
    BOOT_U64 index, found = 0;
    for (index = 0; index < context->Info.MemoryMapBytes/context->Info.MemoryDescriptorBytes; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap,context->Info.MemoryMapBytes,
            context->Info.MemoryDescriptorBytes,index,&region)) return 0;
        if (!kernel_regions_overlap(owner->Physical,1,region.Base,region.Pages)) continue;
        if ((region.Type != 0 && region.Type != 11) || (region.Attributes & 0x3000) ||
            (region.Attributes && !(region.Attributes & 1)) || owner->Physical < region.Base ||
            owner->Physical-region.Base > (region.Pages-1)*4096 || ++found != 1) return 0;
    }
    // LAPIC identificato dal MSR corrente puo essere omesso dalla mappa firmware.
    for (index = 0; index < pool->Count; ++index)
        if (kernel_regions_overlap(owner->Physical,1,pool->Free[index].Base,pool->Free[index].Pages)) return 0;
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
        if (pool->Live[index].Id && kernel_regions_overlap(owner->Physical,1,
            pool->Live[index].Base,pool->Live[index].Pages)) return 0;
    return 1;
}

// ====== owner live e output disgiunti prima di una scansione mista ===== //

static int mixed_inputs(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
    const X86_LAPIC_MAPPING *owner, const X86_PAGING_INVENTORY *output)
{
    return x86_paging_buffers_valid(context,pool,output) && owner &&
        !os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner,sizeof(*owner)) &&
        x86_lapic_authority(owner) && owner->Root == context->Info.PageTableRoot &&
        owner->PhysicalBits == pool->PhysicalBits && device_reserved(context,pool,owner);
}

// ====== fotografia mista pubblicata dopo verifica hardware iniziale e finale ===== //

int x86_paging_inventory_capture_lapic(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *owner, X86_PAGING_INVENTORY *output)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 byte;
    if (!mixed_inputs(context,pool,owner,output) || !scan(context,pool,owner,0,&draft) ||
        !x86_lapic_authority(owner)) return 0;
    for (byte = 0; byte < sizeof(draft); ++byte)
        ((volatile unsigned char *)output)[byte] = ((const unsigned char *)&draft)[byte];
    return 1;
}

// ====== confronto RAM MMIO owner e grafo corrente senza mutazioni PTE ===== //

int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *owner,
    const X86_PAGING_INVENTORY *inventory)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 byte;
    if (!mixed_inputs(context,pool,owner,inventory) || !snapshot_valid(inventory,pool->PhysicalBits) ||
        !scan(context,pool,owner,0,&draft) || !x86_lapic_authority(owner)) return 0;
    for (byte = 0; byte < sizeof(draft); ++byte)
        if (((const unsigned char *)inventory)[byte] != ((const unsigned char *)&draft)[byte]) return 0;
    return 1;
}

// ====== video_inputs ======

static int video_inputs(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
    const X86_LAPIC_MAPPING *lapic, const X86_PAGING_VIDEO_AUTH *video,
    X86_PAGING_VIDEO_CHECK check, const X86_PAGING_INVENTORY *output)
{
    return mixed_inputs(context, pool, lapic, output) && check &&
        x86_paging_video_inputs(context, pool, video, output) &&
        !kernel_regions_overlap(video->Region.Physical, video->Region.AperturePages, lapic->Physical, 1) &&
        !os_address_ranges_overlap((BOOT_U64)video, sizeof(*video), (BOOT_U64)lapic, sizeof(*lapic)) &&
        check(video);
}

// ====== x86_paging_inventory_capture_video ======

int x86_paging_inventory_capture_video(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_VIDEO_AUTH *video, X86_PAGING_VIDEO_CHECK check, X86_PAGING_INVENTORY *output)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 byte;
    if (!video_inputs(context, pool, lapic, video, check, output) ||
        !scan(context, pool, lapic, video, &draft) || !check(video) || !x86_lapic_authority(lapic) ||
        !x86_paging_video_inputs(context, pool, video, output) ||
        !x86_paging_video_tables_complete(video, &draft)) return 0;
    for (byte = 0; byte < sizeof(draft); ++byte)
        ((volatile unsigned char *)output)[byte] = ((const unsigned char *)&draft)[byte];
    return 1;
}

// ====== x86_paging_inventory_verify_video ======

int x86_paging_inventory_verify_video(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_VIDEO_AUTH *video, X86_PAGING_VIDEO_CHECK check,
    const X86_PAGING_INVENTORY *inventory)
{
    X86_PAGING_INVENTORY draft;
    BOOT_U32 byte;
    if (!video_inputs(context, pool, lapic, video, check, inventory) ||
        !snapshot_valid(inventory, pool->PhysicalBits) || !scan(context, pool, lapic, video, &draft) ||
        !check(video) || !x86_lapic_authority(lapic) ||
        !x86_paging_video_inputs(context, pool, video, inventory) ||
        !x86_paging_video_tables_complete(video, &draft)) return 0;
    for (byte = 0; byte < sizeof(draft); ++byte)
        if (((const unsigned char *)inventory)[byte] != ((const unsigned char *)&draft)[byte]) return 0;
    return 1;
}

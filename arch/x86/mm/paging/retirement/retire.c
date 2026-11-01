#include "retire.h"
#include "arch/x86/configs/layout.h"
#include "kernel/diagnostics/log.h"
#include "lib/memory/address_ranges.h"
#define ADDRESS_MASK 0x000ffffffffff000ULL
#define ROOT_VIEW X86_LAYOUT_RECURSIVE_VIEW(X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT)
static X86_PAGING_INVENTORY before, after;

// ====== reserved_private ======

static int reserved_private(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 page, BOOT_U32 type) {
    KERNEL_MEMORY_REGION region;
    BOOT_U32 matches = 0;
    for (BOOT_U64 index = 0; index < context->Info.MemoryMapBytes / context->Info.MemoryDescriptorBytes; ++index) {
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes, context->Info.MemoryDescriptorBytes, index, &region)) return 0;
        if (!kernel_regions_overlap(page, 1, region.Base, region.Pages)) continue;
        if (region.Type != type ||
            !(region.Attributes & 8) ||
            (region.Attributes & ~0x1fULL)
        )
            return 0;
        ++matches;
    }
    if (matches != 1) return 0;
    return kernel_physical_range_unowned(pool, page, 1);
}

// ====== plan_private ======

static int plan_private(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
    BOOT_U64 *capsule, BOOT_U32 *tables) {
    BOOT_U64 data[9], lowest = ~0ULL;
    BOOT_U32 count = 0, code = 0, mask = 0;
    *tables = 0; *capsule = 0;
    for (BOOT_U32 index = 1; index < before.Count; ++index) {
        const X86_PAGING_RESOURCE *record = &before.Tables[index];
        if (!record->Level ||
            record->Level > 3
        )
            return 0;
        if (((record->View >> (12 + 9 * (record->Level - 1))) & 511) != 0) continue;
        ++*tables;
        if (record->Level != 3) continue;
        volatile BOOT_U64 *entries = x86_retire_table(record->View);
        if (!entries) return 0;
        for (BOOT_U32 slot = 0; slot < 512; ++slot) {
            BOOT_U64 entry = entries[slot];
            if (!entry) continue;
            BOOT_U64 address = x86_paging_leaf_virtual(record->View, slot), flags = entry & ~ADDRESS_MASK & ~0x60ULL;
            if (!address ||
                address >= X86_LAYOUT_SLOT_BYTES ||
                (entry & ADDRESS_MASK) != address
            )
                return 0;
            if (flags == 1) {
                if (++code != 1 ||
                    !reserved_private(context, pool, address, 1)
                )
                    return 0;
                *capsule = address;
            }
            else if (flags == ((1ULL << 63) | 3)) {
                if (count == 9 ||
                    !reserved_private(context, pool, address, 2)
                )
                    return 0;
                data[count++] = address;
                if (address < lowest) lowest = address;
            }
            else return 0;
        }
    }
    if (code != 1 ||
        count != 9 ||
        !*tables ||
        lowest > X86_LAYOUT_SLOT_BYTES - 11 * 4096
    )
        return 0;
    for (BOOT_U32 index = 0; index < 9; ++index) {
        BOOT_U64 offset = data[index] - lowest;
        if (offset >= 11 * 4096 ||
            (offset & 4095) ||
            (mask & (1U << (offset / 4096)))
        )
            return 0;
        mask |= 1U << (offset / 4096);
    }
    return mask == 0x7bd && reserved_private(context, pool, lowest + 4096, 2) &&
        reserved_private(context, pool, lowest + 6 * 4096, 2);
}

// ====== field_private ======

static int field_private(const char *name, BOOT_U64 value) {
    return kernel_text(name) && kernel_number(value) && kernel_text("\n");
}

// ====== x86_paging_retire_loader ======

// BSP boot only, IF=0, private snapshots. No pages are freed; a post-commit failure is terminal.
int x86_paging_retire_loader(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool) {
    X86_RETIRE_CPU initial, final;
    BOOT_U64 upper[256], capsule, entry, budget, generation;
    BOOT_U32 tables;
    if (!context ||
        !pool ||
        !context->MemoryMap ||
        context->Info.MemoryDescriptorBytes < 40 ||
        context->Info.MemoryDescriptorBytes > 4096 ||
        !context->Info.MemoryMapBytes ||
        context->Info.MemoryMapBytes > 1048576 ||
        context->Info.MemoryMapBytes % context->Info.MemoryDescriptorBytes ||
        !x86_retire_cpu(&initial) ||
        !x86_retire_cpu_valid(&initial, context->Info.PageTableRoot) ||
        !x86_retire_loaded() ||
        !x86_paging_inventory_capture(context, pool, &before) ||
        before.Ready != 1 ||
        !before.Count ||
        before.Count > X86_BOOT_TABLE_LIMIT ||
        before.Root != initial.Root
    )
        return 0;
    volatile BOOT_U64 *root = x86_retire_table(ROOT_VIEW);
    if (!root ||
        !root[0]
    )
        return 0;
    for (BOOT_U32 slot = 1; slot < 256; ++slot) if (root[slot]) return 0;
    for (BOOT_U32 slot = 0; slot < 256; ++slot) upper[slot] = root[slot + 256] & ~0x60ULL;
    entry = root[0]; if (!kernel_physical_budget_read(pool, &budget)) return 0; generation = x86_paging_generation();
    if (!plan_private(context, pool, &capsule, &tables) ||
        before.Count <= tables ||
        before.LeafPages < 10 ||
        !field_private("KERNEL_RETIRED_CAPSULE: ", capsule)
    )
        return 0;
#ifdef KERNEL_TEST_LOADER_RETIRED
    volatile unsigned char warm = *(volatile unsigned char *)capsule;
    (void)warm;
    if (!kernel_log(1, "KERNEL_RETIRED_CAPSULE_WARMED")) return 0;
#endif
    if (!x86_retire_cpu(&final) ||
        !x86_retire_cpu_valid(&final, initial.Root) ||
        !x86_retire_loaded() ||
        final.Cr4 != initial.Cr4 ||
        !x86_retire_commit(root, initial.Root, entry, initial.Cr4)
    )
        return 0;
    x86_paging_generation_bump();
    if (!x86_retire_cpu(&final) ||
        !x86_retire_cpu_valid(&final, initial.Root) ||
        !x86_retire_loaded() ||
        final.GdtBase != initial.GdtBase ||
        final.IdtBase != initial.IdtBase ||
        !kernel_physical_budget_matches(pool, budget) ||
        x86_paging_generation() != generation + 1
    )
        return 0;
    for (BOOT_U32 slot = 0; slot < 256; ++slot)
        if (root[slot] ||
            (root[slot + 256] & ~0x60ULL) != upper[slot]
        )
            return 0;
    if (!x86_paging_inventory_capture(context, pool, &after) ||
        before.Count - tables != after.Count ||
        before.LeafPages - 10 != after.LeafPages ||
        before.MmioPages != after.MmioPages ||
        !field_private("KERNEL_LOWER_HALF_SLOTS: ", 0) ||
        !field_private("KERNEL_LOWER_HALF_FIRST: ", 512) ||
        !kernel_log(0, "KERNEL_LOWER_HALF_EMPTY") ||
        !kernel_log(0, "KERNEL_LOADER_IDENTITY_RETIRED")
    )
        return 0;
#ifdef KERNEL_TEST_LOADER_RETIRED
    if (!kernel_log(1, "KERNEL_RETIRED_CAPSULE_READ_ARMED")) return 0;
    warm = *(volatile unsigned char *)capsule;
    kernel_panic("EXPECTED_LOADER_FAULT_MISSING");
#else
    return 1;
#endif
}

#include "contents.h"

typedef struct {
    const BOOT_MAPPING_PLAN *Plan;
    const BOOT_PAGE_TABLES *Tables;
    UINT64 Leaves;
    UINT8 Seen[BOOT_TABLE_LIMIT];
} TABLE_VERIFICATION;

// ====== indice di una pagina posseduta senza dereferenziare indirizzi estranei ====== //

static UINT32 owned_index(const BOOT_PAGE_TABLES *tables, UINT64 address)
{
    UINT32 index;
    for (index = 0; index < tables->Count; ++index)
        if (tables->Pages[index] == address) return index;
    return BOOT_TABLE_LIMIT;
}

// ====== corrispondenza esatta di una foglia con il piano ====== //

static int matches_mapping(const BOOT_MAPPING_PLAN *plan, UINT64 virtual_address, UINT64 entry)
{
    UINT32 index;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 offset, expected;
        if (virtual_address < mapping->VirtualBase) continue;
        offset = virtual_address - mapping->VirtualBase;
        if (offset >= mapping->Pages * 4096) continue;
        expected = (mapping->PhysicalBase + offset) | 1ULL |
                   ((mapping->Flags & 2) ? 2ULL : 0ULL) |
                   ((mapping->Flags & 1) ? 0ULL : BOOT_PTE_NX);
        return entry == expected;
    }
    return 0;
}

// ====== visita limitata a quattro livelli e alle pagine possedute ====== //

static EFI_STATUS scan_table(TABLE_VERIFICATION *check, UINT32 index, UINT32 level, UINT64 prefix)
{
    const UINT32 shifts[4] = {39, 30, 21, 12};
    const UINT64 *entries;
    UINT32 slot;
    if (index >= check->Tables->Count || level > 3 || check->Seen[index]) return EFI_DEVICE_ERROR;
    check->Seen[index] = 1;
    entries = (const UINT64 *)(UINTN)check->Tables->Pages[index];
    for (slot = 0; slot < 512; ++slot) {
        UINT64 entry = entries[slot];
        UINT64 virtual_address = prefix | ((UINT64)slot << shifts[level]);
        if (!entry) continue;
        if (level == 0 && slot >= 256) return EFI_DEVICE_ERROR;
        if (level == 3) {
            if (!matches_mapping(check->Plan, virtual_address, entry)) return EFI_DEVICE_ERROR;
            ++check->Leaves;
        } else {
            UINT64 address = entry & 0x000ffffffffff000ULL;
            UINT32 child;
            EFI_STATUS status;
            if (entry != (address | 3ULL)) return EFI_DEVICE_ERROR;
            child = owned_index(check->Tables, address);
            if (child == BOOT_TABLE_LIMIT) return EFI_DEVICE_ERROR;
            status = scan_table(check, child, level + 1, virtual_address);
            if (status != EFI_SUCCESS) return status;
        }
    }
    return EFI_SUCCESS;
}

// ====== metadati verificati prima degli accessi alle tabelle inattive ====== //

EFI_STATUS boot_verify_table_contents(const BOOT_MAPPING_PLAN *plan,
                                     const BOOT_PAGE_TABLES *tables, UINT32 physical_bits)
{
    TABLE_VERIFICATION check;
    UINT32 index, previous, mapping_index;
    UINT64 limit, expected_leaves = 0;
    EFI_STATUS status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS) return status;
    if (!tables || tables->Valid != 1 || !tables->Count || tables->Count > BOOT_TABLE_LIMIT ||
        tables->Root != tables->Pages[0] || physical_bits < 32 || physical_bits > 52)
        return EFI_INVALID_PARAMETER;
    limit = 1ULL << physical_bits;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        if (mapping->PhysicalBase >= limit || mapping->Pages * 4096 > limit - mapping->PhysicalBase)
            return EFI_INVALID_PARAMETER;
        expected_leaves += mapping->Pages;
    }
    for (index = 0; index < tables->Count; ++index) {
        UINT64 address = tables->Pages[index];
        if (!address || (address & 4095) || address > limit - 4096) return EFI_INVALID_PARAMETER;
        if (plan->DescriptorBase && address < plan->DescriptorBase + X86_BOOT_TRANSITION_DATA_PAGES * 4096 &&
            plan->DescriptorBase < address + 4096) return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < index; ++previous)
            if (address == tables->Pages[previous]) return EFI_INVALID_PARAMETER;
        for (mapping_index = 0; mapping_index < plan->Count; ++mapping_index) {
            const BOOT_MAPPING *mapping = &plan->Mappings[mapping_index];
            if (address < mapping->PhysicalBase + mapping->Pages * 4096 &&
                mapping->PhysicalBase < address + 4096) return EFI_INVALID_PARAMETER;
        }
    }
    check.Plan = plan;
    check.Tables = tables;
    check.Leaves = 0;
    for (index = 0; index < BOOT_TABLE_LIMIT; ++index) check.Seen[index] = 0;
    status = scan_table(&check, 0, 0, 0);
    if (status != EFI_SUCCESS) return status;
    if (check.Leaves != expected_leaves) return EFI_DEVICE_ERROR;
    for (index = 0; index < tables->Count; ++index)
        if (!check.Seen[index]) return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}

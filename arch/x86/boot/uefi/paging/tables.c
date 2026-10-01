#include "tables.h"

// ====== allocazione controllata di una tabella da 512 voci ====== //

static EFI_STATUS allocate_table(EFI_BOOT_SERVICES_PREFIX *services,
                                const BOOT_MAPPING_PLAN *plan, UINT64 limit,
                                BOOT_PAGE_TABLES *tables, UINT64 *address)
{
    UINT64 physical = 0;
    UINT32 index;
    EFI_STATUS status;
    if (tables->Count == BOOT_TABLE_LIMIT)
        return EFI_OUT_OF_RESOURCES;
    status = services->AllocatePages(0, 2, 1, &physical);
    if (status != EFI_SUCCESS)
        return status;
    for (index = 0; index < tables->Count; ++index)
        if (tables->Pages[index] == physical)
            return EFI_DEVICE_ERROR;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 end = mapping->PhysicalBase + mapping->Pages * 4096;
        if (physical < end && (physical >= mapping->PhysicalBase ||
            mapping->PhysicalBase - physical < 4096))
            return EFI_DEVICE_ERROR;
    }
    tables->Pages[tables->Count++] = physical;
    if (!physical || (physical & 4095) || physical > limit - 4096)
        return EFI_DEVICE_ERROR;
    for (index = 0; index < 512; ++index)
        ((UINT64 *)(UINTN)physical)[index] = 0;
    *address = physical;
    return EFI_SUCCESS;
}

// ====== percorso PML4 PDPT PD PT per una singola pagina ====== //

static EFI_STATUS map_page(EFI_BOOT_SERVICES_PREFIX *services,
                          const BOOT_MAPPING_PLAN *plan, UINT64 limit,
                          BOOT_PAGE_TABLES *tables, UINT64 root, UINT64 virtual_address,
                          UINT64 physical_address, UINT32 flags)
{
    UINT32 level;
    UINT64 current = root;
    const UINT32 shifts[4] = {39, 30, 21, 12};
    for (level = 0; level < 4; ++level) {
        UINT64 *table = (UINT64 *)(UINTN)current;
        UINTN slot = (virtual_address >> shifts[level]) & 511;
        if (level == 3) {
            if (table[slot])
                return EFI_INVALID_PARAMETER;
            table[slot] = physical_address | 1ULL | ((flags & 2) ? 2ULL : 0ULL) |
                          ((flags & 1) ? 0ULL : BOOT_PTE_NX);
        } else if (!table[slot]) {
            UINT64 next;
            EFI_STATUS status = allocate_table(services, plan, limit, tables, &next);
            if (status != EFI_SUCCESS)
                return status;
            table[slot] = next | 3ULL;
            current = next;
        } else {
            current = table[slot] & 0x000ffffffffff000ULL;
        }
    }
    return EFI_SUCCESS;
}

// ====== costruzione senza modificare CR3 EFER o lo stack corrente ====== //

EFI_STATUS boot_build_page_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                 const BOOT_MAPPING_PLAN *plan, UINT32 physical_bits,
                                 UINT32 nx_supported, BOOT_PAGE_TABLES *tables)
{
    EFI_STATUS status;
    UINT64 limit, root;
    UINT32 index;
    if (!tables || tables->Count || tables->Root || tables->Valid ||
        !services || !services->AllocatePages || !services->FreePages)
        return EFI_INVALID_PARAMETER;
    if (physical_bits < 32 || physical_bits > 52 || nx_supported != 1)
        return EFI_UNSUPPORTED;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS)
        return status;
    limit = 1ULL << physical_bits;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 bytes = mapping->Pages * 4096;
        if (mapping->PhysicalBase >= limit || bytes > limit - mapping->PhysicalBase)
            return EFI_UNSUPPORTED;
    }
    status = allocate_table(services, plan, limit, tables, &root);
    if (status != EFI_SUCCESS)
        return status;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 page;
        for (page = 0; page < mapping->Pages; ++page) {
            status = map_page(services, plan, limit, tables, root,
                              mapping->VirtualBase + page * 4096,
                              mapping->PhysicalBase + page * 4096, mapping->Flags);
            if (status != EFI_SUCCESS)
                return status;
        }
    }
    tables->Root = root;
    tables->Valid = 1;
    return EFI_SUCCESS;
}

// ====== rilascio ripetibile anche dopo un fallimento parziale ====== //

EFI_STATUS boot_release_page_tables(EFI_BOOT_SERVICES_PREFIX *services, BOOT_PAGE_TABLES *tables)
{
    UINT32 index;
    EFI_STATUS result = EFI_SUCCESS;
    if (!tables || tables->Count > BOOT_TABLE_LIMIT)
        return EFI_INVALID_PARAMETER;
    tables->Root = 0;
    tables->Valid = 0;
    if (!tables->Count)
        return EFI_SUCCESS;
    if (!services || !services->FreePages)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < tables->Count; ++index) {
        EFI_STATUS status;
        if (!tables->Pages[index])
            continue;
        status = services->FreePages(tables->Pages[index], 1);
        if (status == EFI_SUCCESS)
            tables->Pages[index] = 0;
        else if (result == EFI_SUCCESS)
            result = status;
    }
    if (result == EFI_SUCCESS)
        tables->Count = 0;
    return result;
}

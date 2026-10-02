#include "tables.h"
#include "verification/contents.h"
#include "lib/memory/address_ranges.h"

// ====== allocazione controllata di una tabella da 512 voci ====== //

static EFI_STATUS allocate_table(EFI_BOOT_SERVICES_PREFIX *services,
                                const BOOT_MAPPING_PLAN *plan, UINT64 limit,
                                BOOT_PAGE_TABLES *tables, UINT64 *address,
                                const BOOT_MAPPING_PLAN *source_plan, const BOOT_MAPPING *source_mapping)
{
    UINT64 physical = 0;
    UINT32 index;
    EFI_STATUS status;
    if (tables->Count == BOOT_TABLE_LIMIT)
        return EFI_OUT_OF_RESOURCES;
    status = services->AllocatePages(0, 2, 1, &physical);
    if (status != EFI_SUCCESS)
        return status;
    if (plan->DescriptorBase && os_address_ranges_overlap(physical, 4096, plan->DescriptorBase, X86_BOOT_TRANSITION_DATA_PAGES * 4096))
        return EFI_DEVICE_ERROR;
    /* Un alias non prova una nuova proprieta: non azzerare ne liberare
       memoria viva. Su firmware non conforme la risorsa resta ambigua
       e puo restare allocata; FreePages potrebbe danneggiare altri owner. */
    if (os_address_ranges_overlap(physical, 4096, (UINT64)(UINTN)tables, sizeof(*tables)) ||
        os_address_ranges_overlap(physical, 4096, (UINT64)(UINTN)plan, sizeof(*plan)) ||
        os_address_ranges_overlap(physical, 4096, (UINT64)(UINTN)services,
                                  services->Hdr.HeaderSize < sizeof(*services) ? sizeof(*services) : services->Hdr.HeaderSize) ||
        os_address_ranges_overlap(physical, 4096, (UINT64)(UINTN)source_plan, source_plan ? sizeof(*source_plan) : 0) ||
        os_address_ranges_overlap(physical, 4096, (UINT64)(UINTN)source_mapping, source_mapping ? sizeof(*source_mapping) : 0))
        return EFI_DEVICE_ERROR;
    for (index = 0; index < tables->Count; ++index)
        if (os_address_ranges_overlap(physical, 4096, tables->Pages[index], 4096))
            return EFI_DEVICE_ERROR;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 end = mapping->PhysicalBase + mapping->Pages * 4096;
        if (physical < end && (physical >= mapping->PhysicalBase ||
            mapping->PhysicalBase - physical < 4096))
            return EFI_DEVICE_ERROR;
    }
    /* Conservare l'output distinto prima dei controlli numerici permette
       al cleanup di tentare FreePages, anche se il limite CPU e superato.
       La registrazione non autorizza ancora l'accesso alla pagina. */
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
                          UINT64 physical_address, UINT32 flags,
                          const BOOT_MAPPING_PLAN *source_plan, const BOOT_MAPPING *source_mapping)
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
            EFI_STATUS status = allocate_table(services, plan, limit, tables, &next, source_plan, source_mapping);
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
    status = allocate_table(services, plan, limit, tables, &root, 0, 0);
    if (status != EFI_SUCCESS)
        return status;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 page;
        for (page = 0; page < mapping->Pages; ++page) {
            status = map_page(services, plan, limit, tables, root,
                              mapping->VirtualBase + page * 4096,
                              mapping->PhysicalBase + page * 4096, mapping->Flags, 0, 0);
            if (status != EFI_SUCCESS)
                return status;
        }
    }
    tables->Root = root;
    tables->Valid = 1;
    return EFI_SUCCESS;
}

// ====== estensione di tabelle inattive dopo verifica del grafo esistente ====== //

EFI_STATUS boot_extend_page_tables(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MAPPING_PLAN *plan,
                                  const BOOT_MAPPING *mapping, UINT32 physical_bits,
                                  BOOT_PAGE_TABLES *tables)
{
    BOOT_MAPPING_PLAN combined;
    BOOT_MAPPING added;
    UINT64 page, root, limit, bytes;
    UINT32 index;
    EFI_STATUS status;
    if (!plan || !mapping || !services || !services->AllocatePages || !services->FreePages)
        return EFI_INVALID_PARAMETER;
    status = boot_verify_table_contents(plan, tables, physical_bits);
    if (status != EFI_SUCCESS) return status;
    if (plan->Count == BOOT_MAPPING_LIMIT) return EFI_OUT_OF_RESOURCES;
    added = *mapping;
    combined = *plan;
    combined.Mappings[combined.Count] = combined.Mappings[combined.Count - 1];
    combined.Mappings[combined.Count - 1] = added;
    ++combined.Count;
    status = boot_validate_mapping_plan(&combined);
    if (status != EFI_SUCCESS) return status;
    limit = 1ULL << physical_bits;
    bytes = added.Pages * 4096;
    if (added.PhysicalBase >= limit || bytes > limit - added.PhysicalBase)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < tables->Count; ++index)
        if (os_address_ranges_overlap(added.PhysicalBase, bytes, tables->Pages[index], 4096))
            return EFI_INVALID_PARAMETER;
    root = tables->Root;
    tables->Valid = 0;
    for (page = 0; page < added.Pages; ++page) {
        status = map_page(services, &combined, limit, tables, root,
                          added.VirtualBase + page * 4096, added.PhysicalBase + page * 4096, added.Flags, plan, mapping);
        if (status != EFI_SUCCESS) { tables->Root = 0; return status; }
    }
    tables->Valid = 1;
    status = boot_verify_table_contents(&combined, tables, physical_bits);
    if (status != EFI_SUCCESS) { tables->Valid = 0; tables->Root = 0; return status; }
    *plan = combined;
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

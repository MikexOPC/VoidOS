#include "preparation.h"
#include "memory/ownership.h"
#include "handoff/resources.h"
#include "verification/contents.h"
#include "memory/final/refresh.h"

// ====== verifica di tutte le pagine delle tabelle nella fotografia viva ====== //

EFI_STATUS boot_check_table_resources(const BOOT_MEMORY_MAP *map,
                                     const BOOT_PAGE_TABLES *tables, UINT32 physical_bits)
{
    UINT32 index, previous;
    UINT64 limit;
    EFI_STATUS status;
    if (!map || map->Valid != 1 || !tables || tables->Valid != 1 ||
        !tables->Count || tables->Count > BOOT_TABLE_LIMIT ||
        tables->Root != tables->Pages[0] || physical_bits < 32 || physical_bits > 52)
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS)
        return status;
    limit = 1ULL << physical_bits;
    for (index = 0; index < tables->Count; ++index) {
        UINT64 address = tables->Pages[index];
        if (!address || (address & 4095) || address > limit - 4096)
            return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < index; ++previous)
            if (address == tables->Pages[previous])
                return EFI_INVALID_PARAMETER;
        status = boot_check_loader_range(map, address, 1);
        if (status != EFI_SUCCESS)
            return status;
    }
    return EFI_SUCCESS;
}

// ====== costruzione seguita da una nuova mappa dopo le allocazioni ====== //

static EFI_STATUS prepare_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                   const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                   const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGE_TABLES *tables,
                                   BOOT_MEMORY_MAP *map, BOOT_START_PAGE *page)
{
    BOOT_MAPPING_PLAN plan;
    EFI_STATUS status, cleanup;
    if (!tables || tables->Count || tables->Root || tables->Valid || !map || map->Buffer || map->Capacity ||
        !capabilities ||
        capabilities->Valid != 1 || capabilities->NxSupported != 1 ||
        capabilities->PhysicalBits < 32 || capabilities->PhysicalBits > 52)
        return EFI_INVALID_PARAMETER;
    status = boot_plan_mappings(kernel, stack, &plan);
    if (status != EFI_SUCCESS)
        return status;
    if (page) {
        status = boot_allocate_start_page(services, &plan, capabilities->PhysicalBits, page);
        if (status == EFI_SUCCESS)
            status = boot_map_start_page(&plan, page);
        if (status != EFI_SUCCESS) return status;
    }
    status = boot_build_page_tables(services, &plan, capabilities->PhysicalBits,
                                   capabilities->NxSupported, tables);
    if (status != EFI_SUCCESS)
        return status;
    status = boot_acquire_memory_map(services, map);
    if (status == EFI_SUCCESS)
        status = boot_refresh_memory_map(services, map);
    if (status == EFI_SUCCESS)
        status = boot_check_resources(map, kernel, stack);
    if (status == EFI_SUCCESS)
        status = boot_check_table_resources(map, tables, capabilities->PhysicalBits);
    if (status == EFI_SUCCESS && page)
        status = boot_check_loader_range(map, page->PhysicalBase, 1);
    if (status == EFI_SUCCESS)
        status = boot_verify_table_contents(&plan, tables, capabilities->PhysicalBits);
    if (status == EFI_SUCCESS && page)
        status = boot_prepare_start_info(&plan, tables, capabilities,
                                         (BOOT_START_INFO *)(UINTN)page->PhysicalBase);
    cleanup = boot_release_memory_map(services, map);
    if (status == EFI_SUCCESS)
        status = cleanup;
    if (status != EFI_SUCCESS) {
        if (page)
            boot_invalidate_start_info((BOOT_START_INFO *)(UINTN)page->PhysicalBase);
        tables->Root = 0;
        tables->Valid = 0;
    }
    if (status == EFI_SUCCESS && page) page->Ready = 1;
    return status;
}

// ====== compatibilita dei test del solo kernel e stack ====== //

EFI_STATUS boot_prepare_page_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                   const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                   const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGE_TABLES *tables,
                                   BOOT_MEMORY_MAP *map)
{
    return prepare_tables(services, kernel, stack, capabilities, tables, map, 0);
}

// ====== percorso boot con pagina contratto nella nuova mappatura ====== //

EFI_STATUS boot_prepare_handoff_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                      const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                      const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGE_TABLES *tables,
                                      BOOT_MEMORY_MAP *map, BOOT_START_PAGE *page)
{
    if (!page) return EFI_INVALID_PARAMETER;
    return prepare_tables(services, kernel, stack, capabilities, tables, map, page);
}

#include "prepare_map.h"
#include "handoff/resources.h"
#include "memory/ownership.h"

// ====== nuova fotografia che copre tutti i proprietari del boot ====== //

EFI_STATUS boot_prepare_final_memory_map(EFI_BOOT_SERVICES_PREFIX *services,
                                        const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                        const BOOT_PAGING_CAPABILITIES *capabilities,
                                        const BOOT_PAGE_TABLES *tables, const BOOT_START_PAGE *page,
                                        BOOT_FINAL_MEMORY_MAP *owner)
{
    BOOT_MAPPING_PLAN plan;
    BOOT_START_PAGE reserved_page;
    EFI_STATUS status;
    if (!owner || !page || page->Ready != 1 || !capabilities || capabilities->Valid != 1 ||
        capabilities->NxSupported != 1 || capabilities->PhysicalBits < 32 || capabilities->PhysicalBits > 52)
        return EFI_INVALID_PARAMETER;
    status = boot_plan_mappings(kernel, stack, &plan);
    if (status != EFI_SUCCESS) return status;
    reserved_page = *page;
    reserved_page.Ready = 0;
    status = boot_map_start_page(&plan, &reserved_page);
    if (status != EFI_SUCCESS) return status;
    status = boot_allocate_final_map(services, &plan, tables, capabilities->PhysicalBits, owner);
    if (status != EFI_SUCCESS) return status;
    status = boot_refresh_final_map(services, owner);
    if (status == EFI_SUCCESS) status = boot_check_resources(&owner->Snapshot, kernel, stack);
    if (status == EFI_SUCCESS) status = boot_check_table_resources(&owner->Snapshot, tables, capabilities->PhysicalBits);
    if (status == EFI_SUCCESS) status = boot_check_loader_range(&owner->Snapshot, page->PhysicalBase, 1);
    if (status != EFI_SUCCESS) boot_forget_map_snapshot(&owner->Snapshot);
    return status;
}

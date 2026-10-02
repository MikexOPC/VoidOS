#include "mapping.h"
#include "handoff/resources.h"
#include "paging/preparation.h"
#include "paging/verification/contents.h"
#include "memory/ownership.h"
#include "lib/memory/address_ranges.h"

// ====== pubblicazione dei metadati da una fotografia numericamente valida ====== //

EFI_STATUS boot_attach_memory_snapshot(BOOT_START_INFO *info, const BOOT_MEMORY_MAP *map)
{
    EFI_STATUS status;
    if (!info || !map || map->Valid != 1 ||
        os_address_ranges_overlap((UINT64)(UINTN)info, sizeof(*info), (UINT64)(UINTN)map, sizeof(*map)) ||
        os_address_ranges_overlap((UINT64)(UINTN)info, sizeof(*info), (UINT64)(UINTN)map->Buffer, map->Capacity))
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) return status;
    if (info->Signature != BOOT_START_SIGNATURE || info->Version != BOOT_START_VERSION ||
        info->Size != sizeof(*info) || info->RequiredFeatures != BOOT_START_FEATURES || info->EnabledFeatures ||
        !map->Capacity || (map->Capacity & 4095)) return EFI_INVALID_PARAMETER;
    info->Signature = 0;
    info->MemoryMapVirtual = BOOT_MAP_VIRTUAL;
    info->MemoryMapBytes = map->Size;
    info->MemoryMapCapacity = map->Capacity;
    info->MemoryDescriptorBytes = map->DescriptorSize;
    info->MemoryDescriptorVersion = map->DescriptorVersion;
    info->MemoryReserved = 0;
    info->Signature = BOOT_START_SIGNATURE;
    return EFI_SUCCESS;
}

// ====== estensione verificata e fotografia dopo le ultime allocazioni ====== //

EFI_STATUS boot_prepare_memory_handoff(
    EFI_BOOT_SERVICES_PREFIX *services,
    const BOOT_KERNEL_IMAGE *kernel,
    const BOOT_KERNEL_STACK *stack,
    const BOOT_PAGING_CAPABILITIES *capabilities,
    BOOT_PAGE_TABLES *tables,
    BOOT_START_PAGE *page,
    BOOT_FINAL_MEMORY_MAP *owner
)

{
    BOOT_MAPPING_PLAN plan;
    BOOT_START_PAGE reserved;
    BOOT_MAPPING mapping;
    BOOT_START_INFO *info;
    EFI_STATUS status;
    if (!owner || !page || page->Ready != 1 || !capabilities || capabilities->Valid != 1 ||
        capabilities->NxSupported != 1 || capabilities->PhysicalBits < 32 || capabilities->PhysicalBits > 52 ||
        !owner->Pages || owner->Pages > X86_BOOT_MAP_MAX_BYTES / 4096 ||
        owner->Snapshot.Capacity != owner->Pages * 4096 || owner->Snapshot.Valid != 1)
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(&owner->Snapshot);
    if (status != EFI_SUCCESS) return status;
    status = boot_plan_mappings(kernel, stack, &plan);
    if (status != EFI_SUCCESS) return status;
    reserved = *page;
    reserved.Ready = 0;
    status = boot_map_start_page(&plan, &reserved);
    if (status != EFI_SUCCESS) return status;
    status = boot_verify_table_contents(&plan, tables, capabilities->PhysicalBits);
    if (status != EFI_SUCCESS) return status;
    info = (BOOT_START_INFO *)(UINTN)page->PhysicalBase;
    boot_invalidate_start_info(info);
    page->Ready = 0;
    mapping.PhysicalBase = (UINT64)(UINTN)owner->Snapshot.Buffer;
    mapping.VirtualBase = BOOT_MAP_VIRTUAL;
    mapping.Pages = owner->Pages;
    mapping.Flags = 4;
    status = boot_extend_page_tables(services, &plan, &mapping, capabilities->PhysicalBits, tables);
    if (status == EFI_SUCCESS) status = boot_refresh_final_map(services, owner);
    if (status == EFI_SUCCESS) status = boot_check_resources(&owner->Snapshot, kernel, stack);
    if (status == EFI_SUCCESS) status = boot_check_table_resources(&owner->Snapshot, tables, capabilities->PhysicalBits);
    if (status == EFI_SUCCESS) status = boot_check_loader_range(&owner->Snapshot, page->PhysicalBase, 1);
    if (status == EFI_SUCCESS) status = boot_verify_table_contents(&plan, tables, capabilities->PhysicalBits);
    if (status == EFI_SUCCESS) status = boot_prepare_start_info(&plan, tables, capabilities, info);
    if (status == EFI_SUCCESS) status = boot_attach_memory_snapshot(info, &owner->Snapshot);
    if (status != EFI_SUCCESS) {
        boot_forget_map_snapshot(&owner->Snapshot);
        boot_invalidate_start_info(info);
        return status;
    }
    page->Ready = 1;
    return EFI_SUCCESS;
}

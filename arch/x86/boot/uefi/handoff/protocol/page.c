#include "page.h"
#include "lib/memory/address_ranges.h"

// ====== inserimento prima dello stack con rollback del piano ====== //

EFI_STATUS boot_map_start_page(BOOT_MAPPING_PLAN *plan, const BOOT_START_PAGE *page)
{
    BOOT_MAPPING saved_stack, saved_tail;
    EFI_STATUS status;
    if (!page || page->Pages != 1 || page->Ready || !page->PhysicalBase ||
        (page->PhysicalBase & 4095) || page->PhysicalBase > ~0ULL - 4096)
        return EFI_INVALID_PARAMETER;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS) return status;
    if (plan->Count == BOOT_MAPPING_LIMIT) return EFI_OUT_OF_RESOURCES;
    saved_stack = plan->Mappings[plan->Count - 1];
    saved_tail = plan->Mappings[plan->Count];
    plan->Mappings[plan->Count] = saved_stack;
    plan->Mappings[plan->Count - 1].PhysicalBase = page->PhysicalBase;
    plan->Mappings[plan->Count - 1].VirtualBase = BOOT_INFO_VIRTUAL;
    plan->Mappings[plan->Count - 1].Pages = 1;
    plan->Mappings[plan->Count - 1].Flags = 4;
    ++plan->Count;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS) {
        --plan->Count;
        plan->Mappings[plan->Count - 1] = saved_stack;
        plan->Mappings[plan->Count] = saved_tail;
    }
    return status;
}

// ====== allocazione EfiLoaderData senza scritture su alias delle risorse ====== //

EFI_STATUS boot_allocate_start_page(EFI_BOOT_SERVICES_PREFIX *services, const BOOT_MAPPING_PLAN *plan, UINT32 physical_bits, BOOT_START_PAGE *page)
{
    EFI_STATUS status;
    UINT64 address = 0, limit;
    UINT32 index;
    if (!page || page->PhysicalBase || page->Pages || page->Ready ||
        physical_bits < 32 || physical_bits > 52)
        return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) ||
        !services->AllocatePages || !services->FreePages) return EFI_UNSUPPORTED;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS) return status;
    status = services->AllocatePages(0, 2, 1, &address);
    if (status != EFI_SUCCESS) return status;
    if (os_address_ranges_overlap(address, 4096, (UINT64)(UINTN)page, sizeof(*page)) ||
        os_address_ranges_overlap(address, 4096, (UINT64)(UINTN)plan, sizeof(*plan)) ||
        os_address_ranges_overlap(address, 4096, (UINT64)(UINTN)services, services->Hdr.HeaderSize))
        return EFI_DEVICE_ERROR;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 end = mapping->PhysicalBase + mapping->Pages * 4096;
        if (address < end && (address >= mapping->PhysicalBase ||
            mapping->PhysicalBase - address < 4096)) return EFI_DEVICE_ERROR;
    }
    page->PhysicalBase = address;
    page->Pages = 1;
    limit = 1ULL << physical_bits;
    if (!address || (address & 4095) || address > limit - 4096) return EFI_DEVICE_ERROR;
    for (index = 0; index < 4096; ++index) ((UINT8 *)(UINTN)address)[index] = 0;
    return EFI_SUCCESS;
}

// ====== rilascio ritentabile senza perdere il proprietario ====== //

EFI_STATUS boot_release_start_page(EFI_BOOT_SERVICES_PREFIX *services, BOOT_START_PAGE *page)
{
    EFI_STATUS status;
    if (!page || page->Pages > 1 || page->Ready > 1 ||
        (page->Ready && (page->Pages != 1 || !page->PhysicalBase || (page->PhysicalBase & 4095))))
        return EFI_INVALID_PARAMETER;
    if (page->Ready == 1)
        boot_invalidate_start_info((BOOT_START_INFO *)(UINTN)page->PhysicalBase);
    page->Ready = 0;
    if (!page->Pages) return EFI_SUCCESS;
    if (!services || !services->FreePages) return EFI_UNSUPPORTED;
    status = services->FreePages(page->PhysicalBase, 1);
    if (status == EFI_SUCCESS) page->PhysicalBase = page->Pages = 0;
    return status;
}

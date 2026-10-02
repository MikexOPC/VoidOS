#include "storage.h"
#include "handoff/firmware/terminal/handoff.h"
#include "lib/memory/address_ranges.h"
#include "memory/regions.h"
#include "handoff/transition/capsule.h"
#include "memory/ownership.h"

// ====== intervallo numerico prima di qualsiasi accesso alle pagine ====== //

static int valid_owner(const BOOT_DESCRIPTOR_STORAGE *owner)
{
    return owner && owner->Pages == BOOT_DESCRIPTOR_PAGES && owner->PhysicalBase >= 4096 &&
           !(owner->PhysicalBase & 4095) &&
           owner->PhysicalBase <= X86_BOOT_LOW_ADDRESS_LIMIT - BOOT_DESCRIPTOR_PAGES * 4096 && owner->Ready <= 1 &&
           !os_address_ranges_overlap((UINT64)(UINTN)owner, sizeof(*owner), owner->PhysicalBase,
                                       BOOT_DESCRIPTOR_PAGES * 4096);
}

// ====== risorse in identita raggiungibili prima e dopo CR3 ====== //

static void make_layout(BOOT_DESCRIPTOR_LAYOUT *layout, const BOOT_DESCRIPTOR_STORAGE *owner)
{
    UINT32 index;
    layout->LinearBase = owner->PhysicalBase;
    layout->PrivilegeStack = owner->PhysicalBase + 2 * 4096;
    layout->ExceptionStack = owner->PhysicalBase + 7 * 4096;
    for (index = 0; index < 32; ++index)
        layout->Targets[index] = owner->CodeBase +
                                 BOOT_CAPSULE_VECTOR_OFFSET + index * BOOT_CAPSULE_VECTOR_STRIDE;
}

// ====== rifiuto degli alias con metadati e firmware prima di adottare ====== //

static int aliases(UINT64 address, UINT64 bytes, EFI_SYSTEM_TABLE *system_table, const BOOT_EXIT_HANDOFF *handoff,
                   BOOT_DESCRIPTOR_STORAGE *owner, const BOOT_MAPPING_PLAN *plan)
{
    EFI_BOOT_SERVICES_PREFIX *services = (EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices;
    const void *objects[] = {owner, plan, handoff, handoff->Kernel, handoff->Stack, handoff->Tables,
                             handoff->Page, handoff->Memory, handoff->Capabilities, system_table,
                             services, system_table->ConOut};
    const UINT64 sizes[] = {sizeof(*owner), sizeof(*plan), sizeof(*handoff), sizeof(*handoff->Kernel),
        sizeof(*handoff->Stack), sizeof(*handoff->Tables), sizeof(*handoff->Page), sizeof(*handoff->Memory),
        sizeof(*handoff->Capabilities), sizeof(*system_table), services->Hdr.HeaderSize,
        sizeof(*system_table->ConOut)};
    UINT32 index;
    for (index = 0; index < sizeof(objects) / sizeof(objects[0]); ++index)
        if (objects[index] && os_address_ranges_overlap(address, bytes, (UINT64)(UINTN)objects[index], sizes[index])) return 1;
    if (system_table->ConOut && system_table->ConOut->Mode &&
        os_address_ranges_overlap(address, bytes, (UINT64)(UINTN)system_table->ConOut->Mode,
                                   sizeof(*system_table->ConOut->Mode))) return 1;
    for (index = 0; index < plan->Count; ++index)
        if (os_address_ranges_overlap(address, bytes, plan->Mappings[index].PhysicalBase,
                                       plan->Mappings[index].Pages * 4096)) return 1;
    for (index = 0; index < handoff->Tables->Count; ++index)
        if (os_address_ranges_overlap(address, bytes, handoff->Tables->Pages[index], 4096)) return 1;
    /* La fotografia precedente protegge anche immagine EFI e risorse firmware non nel piano. */
    for (index = 0; owner && index < handoff->Memory->Snapshot.Size / handoff->Memory->Snapshot.DescriptorSize; ++index) {
        BOOT_MEMORY_REGION region;
        if (boot_read_memory_region(&handoff->Memory->Snapshot, index, &region) != EFI_SUCCESS) return 1;
        if (region.FirmwareType != 7 && os_address_ranges_overlap(address, bytes, region.PhysicalBase,
                                                                   region.Pages * 4096)) return 1;
    }
    return 0;
}

// ====== allocazione distinta registrata solo dopo i controlli ====== //

EFI_STATUS boot_allocate_descriptor_storage(EFI_SYSTEM_TABLE *system_table,
    const BOOT_EXIT_HANDOFF *handoff, BOOT_DESCRIPTOR_STORAGE *owner)
{
    EFI_BOOT_SERVICES_PREFIX *services;
    BOOT_MAPPING_PLAN plan;
    BOOT_DESCRIPTOR_LAYOUT layout;
    UINT64 address = 0, limit;
    UINTN byte;
    EFI_STATUS status;
    if (!owner || owner->PhysicalBase || owner->Pages || owner->Ready || owner->CodeBase || !system_table ||
        !handoff || handoff->Descriptors) return EFI_INVALID_PARAMETER;
    if (system_table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        system_table->Hdr.HeaderSize < sizeof(*system_table)) return EFI_INVALID_PARAMETER;
    services = (EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->AllocatePages || !services->FreePages)
        return EFI_UNSUPPORTED;
    status = boot_validate_exit_handoff(handoff);
    if (status == EFI_SUCCESS) status = boot_check_exit_resources(handoff, &plan);
    if (status != EFI_SUCCESS) return status;
    if (aliases((UINT64)(UINTN)owner, sizeof(*owner), system_table, handoff, 0, &plan))
        return EFI_INVALID_PARAMETER;
    status = services->AllocatePages(0, 2, BOOT_DESCRIPTOR_PAGES, &address);
    if (status != EFI_SUCCESS) return status;
    limit = 1ULL << handoff->Capabilities->PhysicalBits;
    if (limit > X86_BOOT_LOW_ADDRESS_LIMIT) limit = X86_BOOT_LOW_ADDRESS_LIMIT;
    /* Output ambiguo: non adottarlo, scriverlo o liberarlo come se fosse una nuova risorsa. */
    if (address < 4096 || (address & 4095) || address > limit - BOOT_DESCRIPTOR_PAGES * 4096 ||
        aliases(address, BOOT_DESCRIPTOR_PAGES * 4096, system_table, handoff, owner, &plan)) return EFI_DEVICE_ERROR;
    owner->PhysicalBase = address;
    owner->Pages = BOOT_DESCRIPTOR_PAGES;
    for (byte = 0; byte < BOOT_DESCRIPTOR_PAGES * 4096; ++byte) ((UINT8 *)(UINTN)address)[byte] = 0;
    address = 0;
    status = services->AllocatePages(0, 1, 1, &address);
    if (status != EFI_SUCCESS) return status;
    if (address < 4096 || (address & 4095) || address > limit - 4096 ||
        os_address_ranges_overlap(address, 4096, owner->PhysicalBase, owner->Pages * 4096) ||
        aliases(address, 4096, system_table, handoff, owner, &plan)) return EFI_DEVICE_ERROR;
    owner->CodeBase = address;
    status = boot_refresh_final_map(services, handoff->Memory);
    if (status == EFI_SUCCESS) status = boot_check_code_range(&handoff->Memory->Snapshot, address);
    if (status != EFI_SUCCESS) return status;
    if (!boot_prepare_capsule((UINT8 *)(UINTN)address, 4096)) return EFI_DEVICE_ERROR;
    make_layout(&layout, owner);
    if (!boot_prepare_descriptor_bundle((BOOT_DESCRIPTOR_BUNDLE *)(UINTN)owner->PhysicalBase, &layout)) return EFI_DEVICE_ERROR;
    owner->Ready = 1;
    return EFI_SUCCESS;
}

// ====== verifica del contenuto preparato ancora inattivo ====== //

EFI_STATUS boot_check_descriptor_storage(const BOOT_DESCRIPTOR_STORAGE *owner)
{
    BOOT_DESCRIPTOR_LAYOUT layout;
    if (!valid_owner(owner) || owner->Ready != 1) return EFI_INVALID_PARAMETER;
    if (!owner->CodeBase || (owner->CodeBase & 4095) || owner->CodeBase > X86_BOOT_LOW_ADDRESS_LIMIT - 4096 ||
        os_address_ranges_overlap(owner->CodeBase, 4096, owner->PhysicalBase, owner->Pages * 4096) ||
        os_address_ranges_overlap(owner->CodeBase, 4096, (UINT64)(UINTN)owner, sizeof(*owner)) ||
        !boot_validate_capsule((const UINT8 *)(UINTN)owner->CodeBase, 4096))
        return EFI_DEVICE_ERROR;
    make_layout(&layout, owner);
    return boot_validate_descriptor_bundle((const BOOT_DESCRIPTOR_BUNDLE *)(UINTN)owner->PhysicalBase, &layout) ?
           EFI_SUCCESS : EFI_DEVICE_ERROR;
}

// ====== rilascio ritentabile prima di qualsiasi attivazione CPU ====== //

EFI_STATUS boot_release_descriptor_storage(EFI_BOOT_SERVICES_PREFIX *services, BOOT_DESCRIPTOR_STORAGE *owner)
{
    EFI_STATUS status;
    if (!owner) return EFI_INVALID_PARAMETER;
    if (!owner->Pages) return owner->PhysicalBase || owner->Ready || owner->CodeBase ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
    if (!valid_owner(owner)) return EFI_INVALID_PARAMETER;
    if (services && os_address_ranges_overlap(owner->PhysicalBase, BOOT_DESCRIPTOR_PAGES * 4096,
        (UINT64)(UINTN)services, services->Hdr.HeaderSize < sizeof(*services) ? sizeof(*services) : services->Hdr.HeaderSize))
        return EFI_INVALID_PARAMETER;
    boot_invalidate_descriptor_bundle((BOOT_DESCRIPTOR_BUNDLE *)(UINTN)owner->PhysicalBase);
    owner->Ready = 0;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->FreePages) return EFI_UNSUPPORTED;
    if (owner->CodeBase) {
        if ((owner->CodeBase & 4095) || owner->CodeBase < 4096 || owner->CodeBase > X86_BOOT_LOW_ADDRESS_LIMIT - 4096 ||
            os_address_ranges_overlap(owner->CodeBase, 4096, owner->PhysicalBase, owner->Pages * 4096) ||
            os_address_ranges_overlap(owner->CodeBase, 4096, (UINT64)(UINTN)owner, sizeof(*owner)) ||
            os_address_ranges_overlap(owner->CodeBase, 4096, (UINT64)(UINTN)services, services->Hdr.HeaderSize))
            return EFI_INVALID_PARAMETER;
        status = services->FreePages(owner->CodeBase, 1);
        if (status != EFI_SUCCESS) return status;
        owner->CodeBase = 0;
    }
    status = services->FreePages(owner->PhysicalBase, owner->Pages);
    if (status == EFI_SUCCESS) owner->PhysicalBase = owner->Pages = 0;
    return status;
}

// ====== dati RW NX e codice R X inseriti prima dello stack kernel ====== //

EFI_STATUS boot_map_descriptor_storage(BOOT_MAPPING_PLAN *plan, const BOOT_DESCRIPTOR_STORAGE *owner)
{
    BOOT_MAPPING_PLAN draft;
    UINT32 index;
    const UINT64 offsets[] = {0, 2 * 4096, 7 * 4096};
    EFI_STATUS status;
    if (!plan || !owner || os_address_ranges_overlap((UINT64)(UINTN)plan, sizeof(*plan),
        (UINT64)(UINTN)owner, sizeof(*owner))) return EFI_INVALID_PARAMETER;
    if (owner->Pages <= BOOT_DESCRIPTOR_PAGES && os_address_ranges_overlap((UINT64)(UINTN)plan, sizeof(*plan),
        owner->PhysicalBase, owner->Pages * 4096)) return EFI_INVALID_PARAMETER;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS || !valid_owner(owner) || owner->Ready != 1) return EFI_INVALID_PARAMETER;
    if (!owner->CodeBase || (owner->CodeBase & 4095) || owner->CodeBase > X86_BOOT_LOW_ADDRESS_LIMIT - 4096 ||
        os_address_ranges_overlap(owner->CodeBase, 4096, owner->PhysicalBase, owner->Pages * 4096) ||
        os_address_ranges_overlap(owner->CodeBase, 4096, (UINT64)(UINTN)owner, sizeof(*owner)) ||
        os_address_ranges_overlap(owner->CodeBase, 4096, (UINT64)(UINTN)plan, sizeof(*plan))) return EFI_INVALID_PARAMETER;
    if (plan->Count > BOOT_MAPPING_LIMIT - 4) return EFI_OUT_OF_RESOURCES;
    if (!boot_capsule_check_addresses(owner->CodeBase,
                                      plan->Entry, plan->StackTop, BOOT_INFO_VIRTUAL, X86_BOOT_STACK_BYTES))
        return EFI_INVALID_PARAMETER;
    draft = *plan;
    draft.CapsuleBase = owner->CodeBase;
    draft.DescriptorBase = owner->PhysicalBase;
    draft.Mappings[draft.Count + 3] = draft.Mappings[draft.Count - 1];
    for (index = 0; index < 3; ++index) {
        BOOT_MAPPING *mapping = &draft.Mappings[draft.Count - 1 + index];
        mapping->PhysicalBase = owner->PhysicalBase + offsets[index];
        mapping->VirtualBase = mapping->PhysicalBase;
        mapping->Pages = index ? 4 : 1;
        mapping->Flags = 6;
    }
    draft.Mappings[draft.Count + 2].PhysicalBase = owner->CodeBase;
    draft.Mappings[draft.Count + 2].VirtualBase = owner->CodeBase;
    draft.Mappings[draft.Count + 2].Pages = 1;
    draft.Mappings[draft.Count + 2].Flags = 5;
    draft.Count += 4;
    status = boot_validate_mapping_plan(&draft);
    if (status == EFI_SUCCESS) *plan = draft;
    return status;
}

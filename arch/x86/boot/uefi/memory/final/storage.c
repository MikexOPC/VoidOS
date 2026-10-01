#include "storage.h"
#include "memory/ownership.h"
#include "lib/memory/address_ranges.h"
#include "lib/memory/buffer_capacity.h"

// ====== metadati numerici delle risorse prima delle chiamate firmware ====== //

static EFI_STATUS check_reservations(const BOOT_MAPPING_PLAN *plan,
                                     const BOOT_PAGE_TABLES *tables, UINT32 physical_bits)
{
    EFI_STATUS status;
    UINT32 index, previous;
    UINT64 limit;
    if (physical_bits < 32 || physical_bits > 52 || !tables || tables->Valid != 1 ||
        !tables->Count || tables->Count > BOOT_TABLE_LIMIT || tables->Root != tables->Pages[0])
        return EFI_INVALID_PARAMETER;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS) return status;
    limit = 1ULL << physical_bits;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        if (mapping->PhysicalBase > limit - mapping->Pages * 4096)
            return EFI_INVALID_PARAMETER;
    }
    for (index = 0; index < tables->Count; ++index) {
        UINT64 base = tables->Pages[index];
        if (!base || (base & 4095) || base > limit - 4096)
            return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < index; ++previous)
            if (base == tables->Pages[previous]) return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < plan->Count; ++previous)
            if (os_address_ranges_overlap(base, 4096, plan->Mappings[previous].PhysicalBase,
                                          plan->Mappings[previous].Pages * 4096))
                return EFI_INVALID_PARAMETER;
    }
    return EFI_SUCCESS;
}

// ====== dimensionamento con margine e arrotondamento limitati ====== //

static EFI_STATUS query_capacity(EFI_BOOT_SERVICES_PREFIX *services, UINTN *capacity)
{
    UINTN needed = 0, key = 0, stride = 0;
    UINT32 version = 0;
    EFI_STATUS status = services->GetMemoryMap(&needed, 0, &key, &stride, &version);
    if (status != EFI_BUFFER_TOO_SMALL)
        return status == EFI_SUCCESS ? EFI_LOAD_ERROR : status;
    if (!needed || stride < 40 || stride > 4096) return EFI_LOAD_ERROR;
    *capacity = os_buffer_capacity(needed, stride, 8, 4096, BOOT_MAP_MAX_BYTES);
    return *capacity ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}

// ====== nessuna scrittura o nuova proprieta su allocazioni ambigue ====== //

static int overlaps_reservations(UINT64 base, UINTN bytes, const BOOT_MAPPING_PLAN *plan,
                                const BOOT_PAGE_TABLES *tables, const BOOT_FINAL_MEMORY_MAP *owner,
                                const EFI_BOOT_SERVICES_PREFIX *services)
{
    UINT32 index;
    if (os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)owner, sizeof(*owner)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)plan, sizeof(*plan)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)tables, sizeof(*tables)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)services, services->Hdr.HeaderSize))
        return 1;
    for (index = 0; index < plan->Count; ++index)
        if (os_address_ranges_overlap(base, bytes, plan->Mappings[index].PhysicalBase,
                                      plan->Mappings[index].Pages * 4096)) return 1;
    for (index = 0; index < tables->Count; ++index)
        if (os_address_ranges_overlap(base, bytes, tables->Pages[index], 4096)) return 1;
    return 0;
}

// ====== allocazione EfiLoaderData separata dai buffer AllocatePool ====== //

EFI_STATUS boot_allocate_final_map(EFI_BOOT_SERVICES_PREFIX *services,
                                  const BOOT_MAPPING_PLAN *plan,
                                  const BOOT_PAGE_TABLES *tables, UINT32 physical_bits,
                                  BOOT_FINAL_MEMORY_MAP *owner)
{
    UINTN capacity, byte;
    UINT64 address = 0, limit;
    EFI_STATUS status;
    if (!owner || owner->Pages || owner->Snapshot.Buffer || owner->Snapshot.Capacity ||
        owner->Snapshot.Size || owner->Snapshot.Key || owner->Snapshot.DescriptorSize ||
        owner->Snapshot.DescriptorVersion || owner->Snapshot.Valid) return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->GetMemoryMap ||
        !services->AllocatePages || !services->FreePages) return EFI_UNSUPPORTED;
    status = check_reservations(plan, tables, physical_bits);
    if (status != EFI_SUCCESS) return status;
    status = query_capacity(services, &capacity);
    if (status != EFI_SUCCESS) return status;
    status = services->AllocatePages(0, 2, capacity / 4096, &address);
    if (status != EFI_SUCCESS) return status;
    if (overlaps_reservations(address, capacity, plan, tables, owner, services))
        return EFI_DEVICE_ERROR;
    owner->Snapshot.Buffer = (void *)(UINTN)address;
    owner->Snapshot.Capacity = capacity;
    owner->Pages = capacity / 4096;
    limit = 1ULL << physical_bits;
    if (!address || (address & 4095) || address > limit - capacity) return EFI_DEVICE_ERROR;
    for (byte = 0; byte < capacity; ++byte) ((UINT8 *)(UINTN)address)[byte] = 0;
    return EFI_SUCCESS;
}

// ====== fotografia fresca e copertura delle proprie pagine ====== //

EFI_STATUS boot_refresh_final_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_FINAL_MEMORY_MAP *owner)
{
    EFI_STATUS status;
    if (!owner) return EFI_INVALID_PARAMETER;
    boot_forget_map_snapshot(&owner->Snapshot);
    if (!owner->Pages || owner->Pages > BOOT_MAP_MAX_BYTES / 4096 ||
        !owner->Snapshot.Buffer || ((UINTN)owner->Snapshot.Buffer & 4095) ||
        owner->Snapshot.Capacity != owner->Pages * 4096)
        return EFI_INVALID_PARAMETER;
    status = boot_refresh_memory_map(services, &owner->Snapshot);
    if (status == EFI_SUCCESS)
        status = boot_check_loader_range(&owner->Snapshot, (UINT64)(UINTN)owner->Snapshot.Buffer, owner->Pages);
    if (status != EFI_SUCCESS) boot_forget_map_snapshot(&owner->Snapshot);
    return status;
}

// ====== rilascio ritentabile solo mentre i servizi UEFI sono disponibili ====== //

EFI_STATUS boot_release_final_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_FINAL_MEMORY_MAP *owner)
{
    EFI_STATUS status;
    if (!owner) return EFI_INVALID_PARAMETER;
    boot_forget_map_snapshot(&owner->Snapshot);
    if (!owner->Pages)
        return owner->Snapshot.Buffer || owner->Snapshot.Capacity ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
    if (owner->Pages > BOOT_MAP_MAX_BYTES / 4096 || owner->Snapshot.Capacity != owner->Pages * 4096)
        return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->FreePages)
        return EFI_UNSUPPORTED;
    status = services->FreePages((UINT64)(UINTN)owner->Snapshot.Buffer, owner->Pages);
    if (status == EFI_SUCCESS) {
        owner->Snapshot.Buffer = 0;
        owner->Snapshot.Capacity = owner->Pages = 0;
    }
    return status;
}

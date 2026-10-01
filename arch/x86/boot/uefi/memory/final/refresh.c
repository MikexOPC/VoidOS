#include "refresh.h"
#include "lib/memory/buffer_capacity.h"
#include "lib/memory/address_ranges.h"

// ====== invalidazione della fotografia senza perdere il buffer ====== //

void boot_forget_map_snapshot(BOOT_MEMORY_MAP *map)
{
    if (!map) return;
    map->Size = map->Key = map->DescriptorSize = 0;
    map->DescriptorVersion = map->Valid = 0;
}

// ====== aggiornamento nel buffer posseduto senza allocazioni o rilasci ====== //

EFI_STATUS boot_refresh_memory_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MEMORY_MAP *map)
{
    UINTN needed, key = 0, stride = 0;
    UINT32 version = 0;
    EFI_STATUS status;
    if (!map) return EFI_INVALID_PARAMETER;
    boot_forget_map_snapshot(map);
    if (!os_buffer_extent_is_valid((UINT64)(UINTN)map->Buffer, map->Capacity, BOOT_MAP_MAX_BYTES) ||
        os_address_ranges_overlap((UINT64)(UINTN)map->Buffer, map->Capacity, (UINT64)(UINTN)map, sizeof(*map)))
        return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->GetMemoryMap)
        return EFI_UNSUPPORTED;
    if (os_address_ranges_overlap((UINT64)(UINTN)map->Buffer, map->Capacity,
                                  (UINT64)(UINTN)services, services->Hdr.HeaderSize)) return EFI_INVALID_PARAMETER;
    needed = map->Capacity;
    status = services->GetMemoryMap(&needed, map->Buffer, &key, &stride, &version);
    if (status != EFI_SUCCESS) return status;
    map->Size = needed;
    map->Key = key;
    map->DescriptorSize = stride;
    map->DescriptorVersion = version;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) {
        boot_forget_map_snapshot(map);
        return status;
    }
    map->Valid = 1;
    return EFI_SUCCESS;
}

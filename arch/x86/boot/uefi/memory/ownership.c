#include "ownership.h"

#define EFI_LOADER_DATA_TYPE 2U
#define EFI_RUNTIME_ATTRIBUTE (1ULL << 63)

// ====== copertura anche su descrittori adiacenti e non ordinati ====== //

static EFI_STATUS check_range(const BOOT_MEMORY_MAP *map, UINT64 base, UINTN pages, UINT32 type)
{
    UINT64 end, cursor = base;
    UINTN index, steps, count;
    if (!map || map->Valid != 1 || !map->Buffer || !pages || (base & 4095) ||
        pages > (~0ULL - base) / 4096 || map->DescriptorSize < 40 ||
        map->DescriptorSize > 4096 || !map->Size || map->Size > map->Capacity ||
        map->Capacity > BOOT_MAP_MAX_BYTES || map->Size % map->DescriptorSize)
        return EFI_INVALID_PARAMETER;
    end = base + pages * 4096;
    count = map->Size / map->DescriptorSize;
    if (count > BOOT_MAP_MAX_DESCRIPTORS)
        return EFI_OUT_OF_RESOURCES;
    for (steps = 0; cursor < end && steps < count; ++steps) {
        int found = 0;
        for (index = 0; index < count; ++index) {
            BOOT_MEMORY_REGION region;
            UINT64 region_end;
            EFI_STATUS status = boot_read_memory_region(map, index, &region);
            if (status != EFI_SUCCESS)
                return status;
            region_end = region.PhysicalBase + region.Pages * 4096;
            if (region.PhysicalBase > cursor || region_end <= cursor)
                continue;
            if (region.FirmwareType != type || (region.Attributes & EFI_RUNTIME_ATTRIBUTE) ||
                (type == 1 && (region.Attributes & ((1ULL << 14) | (1ULL << 13) | (1ULL << 17)))))
                return EFI_LOAD_ERROR;
            cursor = region_end < end ? region_end : end;
            found = 1;
            break;
        }
        if (!found)
            return EFI_LOAD_ERROR;
    }
    return cursor == end ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

// ====== dati posseduti e codice leggibile scrivibile eseguibile nel firmware ====== //

EFI_STATUS boot_check_loader_range(const BOOT_MEMORY_MAP *map, UINT64 base, UINTN pages)
{
    return check_range(map, base, pages, EFI_LOADER_DATA_TYPE);
}

EFI_STATUS boot_check_code_range(const BOOT_MEMORY_MAP *map, UINT64 base)
{
    return check_range(map, base, 1, 1);
}

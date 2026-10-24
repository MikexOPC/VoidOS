#include "regions.h"
#include "lib/memory/buffer_capacity.h"
#include "lib/firmware/uefi/memory.h"

#define EFI_CONVENTIONAL_MEMORY 7U
#define ALLOWED_CONVENTIONAL_ATTRIBUTES OS_UEFI_CONVENTIONAL_ATTRIBUTES

// ====== decodifica little endian senza cast del buffer ====== //

static UINT64 read_integer(const UINT8 *bytes, UINTN length)
{
    UINT64 value = 0;
    UINTN index;
    for (index = 0; index < length; ++index)
        value |= (UINT64)bytes[index] << (index * 8);
    return value;
}

// ====== lettura limitata di una regione da una fotografia viva ====== //

EFI_STATUS boot_read_memory_region(const BOOT_MEMORY_MAP *map, UINTN index, BOOT_MEMORY_REGION *region)
{
    BOOT_MEMORY_REGION result;
    const UINT8 *descriptor;
    if (!region)
        return EFI_INVALID_PARAMETER;
    region->PhysicalBase = region->Pages = region->Attributes = 0;
    region->FirmwareType = region->ConventionalCandidate = 0;
    if (!map || map->Valid != 1 || !map->Buffer || !map->Size ||
        !os_buffer_extent_is_valid((UINT64)(UINTN)map->Buffer, map->Capacity, BOOT_MAP_MAX_BYTES) ||
        map->Size > map->Capacity ||
        map->DescriptorVersion != 1 || map->DescriptorSize < 40 || map->DescriptorSize > 4096 ||
        map->Size % map->DescriptorSize ||
        map->Size / map->DescriptorSize > BOOT_MAP_MAX_DESCRIPTORS ||
        index >= map->Size / map->DescriptorSize)
        return EFI_INVALID_PARAMETER;
    descriptor = (const UINT8 *)map->Buffer + index * map->DescriptorSize;
    result.FirmwareType = (UINT32)read_integer(descriptor, 4);
    result.PhysicalBase = read_integer(descriptor + 8, 8);
    result.Pages = read_integer(descriptor + 24, 8);
    result.Attributes = read_integer(descriptor + 32, 8);
    if ((result.PhysicalBase & 4095) || !result.Pages ||
        result.Pages > (~0ULL - result.PhysicalBase) / 4096)
        return EFI_LOAD_ERROR;
    result.ConventionalCandidate = result.FirmwareType == EFI_CONVENTIONAL_MEMORY &&
        !(result.Attributes & ~ALLOWED_CONVENTIONAL_ATTRIBUTES);
    *region = result;
    return EFI_SUCCESS;
}

// ====== conteggio solo dopo la verifica completa degli intervalli ====== //

EFI_STATUS boot_summarize_memory(const BOOT_MEMORY_MAP *map, BOOT_MEMORY_SUMMARY *summary)
{
    BOOT_MEMORY_SUMMARY result = {0};
    BOOT_MEMORY_REGION region;
    EFI_STATUS status;
    UINTN index, count;
    if (!summary)
        return EFI_INVALID_PARAMETER;
    summary->ConventionalCandidatePages = summary->ExcludedPages = summary->RegionCount = 0;
    if (!map || map->Valid != 1)
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS)
        return status;
    count = map->Size / map->DescriptorSize;
    for (index = 0; index < count; ++index) {
        UINT64 *total;
        status = boot_read_memory_region(map, index, &region);
        if (status != EFI_SUCCESS)
            return status;
        total = region.ConventionalCandidate ? &result.ConventionalCandidatePages : &result.ExcludedPages;
        if (region.Pages > ~0ULL - *total)
            return EFI_LOAD_ERROR;
        *total += region.Pages;
    }
    result.RegionCount = count;
    *summary = result;
    return EFI_SUCCESS;
}

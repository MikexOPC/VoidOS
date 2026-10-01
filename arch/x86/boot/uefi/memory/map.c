#include "map.h"
#include "lib/memory/address_ranges.h"
#include "lib/memory/buffer_capacity.h"

// ====== invalidazione senza perdita del buffer posseduto ====== //

static void invalidate(BOOT_MEMORY_MAP *map)
{
    map->Size = 0;
    map->Key = 0;
    map->DescriptorSize = 0;
    map->DescriptorVersion = 0;
    map->Valid = 0;
}

EFI_STATUS boot_release_memory_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MEMORY_MAP *map)
{
    EFI_STATUS status;
    if (!map)
        return EFI_INVALID_PARAMETER;
    invalidate(map);
    if (!map->Buffer) {
        map->Capacity = 0;
        return EFI_SUCCESS;
    }
    if (!services || !services->FreePool)
        return EFI_UNSUPPORTED;
    status = services->FreePool(map->Buffer);
    if (status == EFI_SUCCESS) {
        map->Buffer = 0;
        map->Capacity = 0;
    }
    return status;
}

// ====== decodifica del prefisso senza cast o accessi disallineati ====== //

static UINT64 read64(const UINT8 *bytes)
{
    UINT64 value = 0;
    UINTN index;
    for (index = 0; index < 8; ++index)
        value |= (UINT64)bytes[index] << (index * 8);
    return value;
}

EFI_STATUS boot_validate_memory_map(const BOOT_MEMORY_MAP *map)
{
    const UINT8 *buffer;
    UINTN index, previous;
    UINTN count;
    UINT64 previous_base = 0, previous_end = 0;
    int ordered = 1;
    if (!map || !map->Buffer)
        return EFI_INVALID_PARAMETER;
    buffer = (const UINT8 *)map->Buffer;
    if (!os_buffer_extent_is_valid((UINT64)(UINTN)buffer, map->Capacity, BOOT_MAP_MAX_BYTES) ||
        !map->Size || map->Size > map->Capacity || map->DescriptorSize < 40 ||
        map->DescriptorSize > 4096 || map->DescriptorVersion != 1 ||
        map->Size % map->DescriptorSize)
        return EFI_LOAD_ERROR;
    count = map->Size / map->DescriptorSize;
    if (count > BOOT_MAP_MAX_DESCRIPTORS)
        return EFI_OUT_OF_RESOURCES;
    for (index = 0; index < count; ++index) {
        const UINT8 *descriptor = buffer + index * map->DescriptorSize;
        UINT64 base = read64(descriptor + 8);
        UINT64 pages = read64(descriptor + 24);
        UINT64 bytes;
        if ((base & 4095) || !pages || pages > (~0ULL - base) / 4096)
            return EFI_LOAD_ERROR;
        bytes = pages * 4096;
        if (index && base < previous_base) ordered = 0;
        if (ordered && index && base < previous_end) return EFI_LOAD_ERROR;
        for (previous = 0; !ordered && previous < index; ++previous) {
            const UINT8 *other = buffer + previous * map->DescriptorSize;
            UINT64 other_base = read64(other + 8);
            UINT64 other_bytes = read64(other + 24) * 4096;
            if (os_address_ranges_overlap(base, bytes, other_base, other_bytes))
                return EFI_LOAD_ERROR;
        }
        previous_base = base;
        previous_end = base + bytes;
    }
    return EFI_SUCCESS;
}

// ====== dimensionamento limitato con margine per la crescita ====== //

static EFI_STATUS capacity_for(UINTN needed, UINTN stride, UINTN *capacity)
{
    if (!needed || stride < 40 || stride > 4096)
        return EFI_LOAD_ERROR;
    *capacity = os_buffer_capacity(needed, stride, 8, 1, BOOT_MAP_MAX_BYTES);
    return *capacity ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}

// ====== acquisizione senza pubblicare risultati parziali ====== //

EFI_STATUS boot_acquire_memory_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MEMORY_MAP *map)
{
    UINTN needed = 0, key = 0, stride = 0, capacity = 0, attempt;
    UINT32 version = 0;
    EFI_STATUS status;
    if (!map || map->Buffer || map->Capacity)
        return EFI_INVALID_PARAMETER;
    invalidate(map);
    if (!services || services->Hdr.HeaderSize < sizeof(*services) ||
        !services->GetMemoryMap || !services->AllocatePool || !services->FreePool)
        return EFI_UNSUPPORTED;
    status = services->GetMemoryMap(&needed, 0, &key, &stride, &version);
    if (status != EFI_BUFFER_TOO_SMALL)
        return status == EFI_SUCCESS ? EFI_LOAD_ERROR : status;
    for (attempt = 0; attempt < 4; ++attempt) {
        void *allocated = 0;
        status = capacity_for(needed, stride, &capacity);
        if (status != EFI_SUCCESS)
            return status;
        status = boot_release_memory_map(services, map);
        if (status != EFI_SUCCESS)
            return status;
        status = services->AllocatePool(2, capacity, &allocated);
        if (status != EFI_SUCCESS)
            return status;
        if (!allocated)
            return EFI_OUT_OF_RESOURCES;
        if (os_address_ranges_overlap((UINT64)(UINTN)allocated, capacity, (UINT64)(UINTN)map, sizeof(*map)) ||
            os_address_ranges_overlap((UINT64)(UINTN)allocated, capacity, (UINT64)(UINTN)services, services->Hdr.HeaderSize))
            return EFI_DEVICE_ERROR;
        map->Buffer = allocated;
        map->Capacity = capacity;
        if (!os_buffer_extent_is_valid((UINT64)(UINTN)allocated, capacity, BOOT_MAP_MAX_BYTES))
            return EFI_DEVICE_ERROR;
        needed = capacity;
        stride = key = 0;
        version = 0;
        status = services->GetMemoryMap(&needed, allocated, &key, &stride, &version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            if (needed <= capacity)
                return EFI_LOAD_ERROR;
            continue;
        }
        if (status != EFI_SUCCESS)
            return status;
        map->Size = needed;
        map->Key = key;
        map->DescriptorSize = stride;
        map->DescriptorVersion = version;
        status = boot_validate_memory_map(map);
        if (status != EFI_SUCCESS) {
            invalidate(map);
            return status;
        }
        map->Valid = 1;
        return EFI_SUCCESS;
    }
    return EFI_OUT_OF_RESOURCES;
}

#include "load.h"
#include "lib/memory/address_ranges.h"

// ====== verifica delle allocazioni gia possedute ====== //

static int conflicts_with_owned_memory(const ELF_STAGED_IMAGE *image, const UINT8 *file, UINTN size, UINT64 address, UINT64 bytes)
{
    UINT32 index;
    if (os_address_ranges_overlap(address, bytes, (UINT64)(UINTN)file, size) ||
        os_address_ranges_overlap(address, bytes, (UINT64)(UINTN)image, sizeof(*image))) return 1;
    for (index = 0; index < image->Count; ++index) {
        const ELF_STAGED_SEGMENT *segment = &image->Segments[index];
        if (os_address_ranges_overlap(address, bytes, segment->PhysicalBase, segment->Pages * 4096)) return 1;
    }
    return 0;
}

// ====== rilascio di tutte le allocazioni acquisite ====== //

EFI_STATUS elf_release_segments(EFI_BOOT_SERVICES_PREFIX *services, ELF_STAGED_IMAGE *image)
{
    UINTN index;
    EFI_STATUS result = EFI_SUCCESS;
    if (!services || !services->FreePages || !image || image->Count > ELF_MAX_LOAD_SEGMENTS)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < image->Count; ++index) {
        ELF_STAGED_SEGMENT *segment = &image->Segments[index];
        EFI_STATUS status;
        if (!segment->Pages)
            continue;
        status = services->FreePages(segment->PhysicalBase, segment->Pages);
        if (status == EFI_SUCCESS) {
            image->AllocatedBytes -= segment->Pages * 4096;
            segment->Pages = 0;
        } else if (result == EFI_SUCCESS) {
            result = status;
        }
    }
    if (result == EFI_SUCCESS)
        image->Count = 0;
    return result;
}

// ====== controllo budget includendo offset e arrotondamento ====== //

static EFI_STATUS check_budget(const ELF_LOAD_PLAN *plan)
{
    UINTN index;
    UINT64 total = 0;
    for (index = 0; index < plan->SegmentCount; ++index) {
        const ELF_LOAD_SEGMENT *segment = &plan->Segments[index];
        UINT64 bytes = (segment->MemoryBytes + (segment->VirtualAddress & 4095) + 4095) & ~4095ULL;
        if (bytes > ELF_MAX_MEMORY_BYTES - total)
            return EFI_OUT_OF_RESOURCES;
        total += bytes;
    }
    return EFI_SUCCESS;
}

// ====== allocazione e copia senza esecuzione ====== //

EFI_STATUS elf_stage_segments(EFI_BOOT_SERVICES_PREFIX *services, const UINT8 *file,
                              UINTN size, ELF_STAGED_IMAGE *image)
{
    ELF_LOAD_PLAN plan;
    UINTN index;
    EFI_STATUS status;
    if (!image)
        return EFI_INVALID_PARAMETER;
    if (image->Count || image->AllocatedBytes)
        return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) ||
        !services->AllocatePages || !services->FreePages)
        return EFI_UNSUPPORTED;
    if (elf_validate(file, size, &plan) != ELF_VALID)
        return EFI_LOAD_ERROR;
    status = check_budget(&plan);
    if (status != EFI_SUCCESS)
        return status;
    for (index = 0; index < plan.SegmentCount; ++index) {
        const ELF_LOAD_SEGMENT *segment = &plan.Segments[index];
        ELF_STAGED_SEGMENT *staged = &image->Segments[index];
        UINT64 offset = segment->VirtualAddress & 4095;
        UINT64 address = 0;
        UINTN pages = (UINTN)((offset + segment->MemoryBytes + 4095) / 4096);
        UINT8 *destination;
        UINTN byte;
        status = services->AllocatePages(0, 2, pages, &address);
        if (status != EFI_SUCCESS)
            break;
        if (conflicts_with_owned_memory(image, file, size, address, pages * 4096) ||
            os_address_ranges_overlap(address, pages * 4096, (UINT64)(UINTN)&plan, sizeof(plan))) {
            status = EFI_DEVICE_ERROR;
            break;
        }
        staged->PhysicalBase = address;
        staged->VirtualBase = segment->VirtualAddress & ~4095ULL;
        staged->Pages = pages;
        staged->Flags = segment->Flags;
        ++image->Count;
        image->AllocatedBytes += pages * 4096;
        if (!address || (address & 4095) || address > ~0ULL - pages * 4096) {
            status = EFI_DEVICE_ERROR;
            break;
        }
        destination = (UINT8 *)address;
        for (byte = 0; byte < pages * 4096; ++byte)
            destination[byte] = 0;
        for (byte = 0; byte < segment->FileBytes; ++byte)
            destination[offset + byte] = file[segment->FileOffset + byte];
    }
    if (status != EFI_SUCCESS)
        elf_release_segments(services, image);
    return status;
}

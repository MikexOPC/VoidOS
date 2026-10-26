#include "segments.h"
#include "arch/x86/configs/layout.h"

// ====== invalidazione del risultato prima dei controlli ====== //

static void clear_description(BOOT_SEGMENTS *description)
{
    UINTN index;
    description->Signature = 0;
    description->Version = 0;
    description->Size = 0;
    description->VirtualEntry = 0;
    description->AllocatedBytes = 0;
    description->SegmentCount = 0;
    description->Reserved = 0;
    for (index = 0; index < ELF_MAX_LOAD_SEGMENTS; ++index) {
        description->Segments[index].PhysicalBase = 0;
        description->Segments[index].VirtualBase = 0;
        description->Segments[index].Bytes = 0;
        description->Segments[index].Flags = 0;
        description->Segments[index].Reserved = 0;
    }
}

// ====== controllo corrispondenza tra piano e pagine ancora possedute ====== //

EFI_STATUS boot_describe_segments(const ELF_LOAD_PLAN *plan, const ELF_STAGED_IMAGE *image,
                                  BOOT_SEGMENTS *description)
{
    UINTN index, previous;
    UINT64 total = 0;
    UINT64 memory = 0;
    int executable_entry = 0;
    if (!description)
        return EFI_INVALID_PARAMETER;
    clear_description(description);
    if (!plan || !image || !image->Count || image->Count > ELF_MAX_LOAD_SEGMENTS ||
        plan->SegmentCount != image->Count)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < image->Count; ++index) {
        const ELF_STAGED_SEGMENT *staged = &image->Segments[index];
        const ELF_LOAD_SEGMENT *segment = &plan->Segments[index];
        UINT64 bytes;
        UINT64 expected;
        if (!segment->MemoryBytes || segment->MemoryBytes > ELF_MAX_MEMORY_BYTES - memory ||
            segment->FileBytes > segment->MemoryBytes ||
            !X86_LAYOUT_RANGE(segment->VirtualAddress, segment->MemoryBytes) ||  // K1: either half
            (segment->Flags & ~7U) || !(segment->Flags & 4) || (segment->Flags & 3) == 3)
            return EFI_INVALID_PARAMETER;
        memory += segment->MemoryBytes;
        expected = (segment->MemoryBytes + (segment->VirtualAddress & 4095) + 4095) & ~4095ULL;
        if (!staged->Pages || staged->Pages > ELF_MAX_MEMORY_BYTES / 4096 ||
            !staged->PhysicalBase || (staged->PhysicalBase & 4095) ||
            staged->VirtualBase != (segment->VirtualAddress & ~4095ULL) ||
            staged->Flags != segment->Flags)
            return EFI_INVALID_PARAMETER;
        bytes = staged->Pages * 4096;
        if (bytes != expected || bytes > ELF_MAX_MEMORY_BYTES - total ||
            staged->PhysicalBase > ~0ULL - bytes)
            return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < index; ++previous) {
            const ELF_STAGED_SEGMENT *other = &image->Segments[previous];
            UINT64 other_bytes = other->Pages * 4096;
            if ((staged->PhysicalBase < other->PhysicalBase + other_bytes &&
                 other->PhysicalBase < staged->PhysicalBase + bytes) ||
                staged->VirtualBase < other->VirtualBase + other_bytes)
                return EFI_INVALID_PARAMETER;
        }
        total += bytes;
        if ((segment->Flags & 1) && plan->Entry >= segment->VirtualAddress &&
            plan->Entry - segment->VirtualAddress < segment->FileBytes)
            executable_entry = 1;
    }
    if (!executable_entry || total != image->AllocatedBytes || memory != plan->TotalMemoryBytes)
        return EFI_INVALID_PARAMETER;

    // ====== pubblicazione solo dopo tutti i controlli ====== //

    description->Version = BOOT_SEGMENTS_VERSION;
    description->Size = sizeof(*description);
    description->VirtualEntry = plan->Entry;
    description->AllocatedBytes = total;
    description->SegmentCount = image->Count;
    for (index = 0; index < image->Count; ++index) {
        description->Segments[index].PhysicalBase = image->Segments[index].PhysicalBase;
        description->Segments[index].VirtualBase = image->Segments[index].VirtualBase;
        description->Segments[index].Bytes = image->Segments[index].Pages * 4096;
        description->Segments[index].Flags = image->Segments[index].Flags;
    }
    description->Signature = BOOT_SEGMENTS_SIGNATURE;
    return EFI_SUCCESS;
}

#include "resources.h"
#include "arch/x86/configs/layout.h"

// ====== corrispondenza del contratto con le risorse ancora possedute ====== //

EFI_STATUS boot_check_resources(const BOOT_MEMORY_MAP *map, const BOOT_KERNEL_IMAGE *kernel,
                                const BOOT_KERNEL_STACK *stack)
{
    EFI_STATUS status;
    UINTN index, previous;
    UINT64 total = 0, stack_end;
    if (!map || map->Valid != 1 || !kernel || !stack || kernel->FileBuffer || kernel->FileBufferBytes ||
        kernel->Description.Signature != BOOT_SEGMENTS_SIGNATURE ||
        kernel->Description.Version != BOOT_SEGMENTS_VERSION ||
        kernel->Description.Size != sizeof(kernel->Description) ||
        kernel->Description.Reserved || !X86_LAYOUT_RANGE(kernel->Description.VirtualEntry, 1) ||
        !kernel->Pages.Count || kernel->Pages.Count > ELF_MAX_LOAD_SEGMENTS ||
        kernel->Description.SegmentCount != kernel->Pages.Count ||
        !stack->PhysicalBase || (stack->PhysicalBase & 4095) ||
        stack->Pages != BOOT_STACK_PAGES ||
        stack->PhysicalBase > ~0ULL - stack->Pages * 4096)
        return EFI_INVALID_PARAMETER;
    stack_end = stack->PhysicalBase + stack->Pages * 4096;
    if (stack->Top != stack_end || (stack->Top & 15))
        return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS)
        return status;
    for (index = 0; index < kernel->Pages.Count; ++index) {
        const ELF_STAGED_SEGMENT *segment = &kernel->Pages.Segments[index];
        const BOOT_SEGMENT *description = &kernel->Description.Segments[index];
        UINT64 bytes;
        if (!segment->PhysicalBase || (segment->PhysicalBase & 4095) || !segment->Pages ||
            segment->Pages > ELF_MAX_MEMORY_BYTES / 4096 || (segment->VirtualBase & 4095) ||
            !X86_LAYOUT_RANGE(segment->VirtualBase, 4096) ||
            (segment->Flags & ~7U) || !(segment->Flags & 4) || (segment->Flags & 3) == 3)
            return EFI_INVALID_PARAMETER;
        bytes = segment->Pages * 4096;
        if (bytes > ELF_MAX_MEMORY_BYTES - total || segment->PhysicalBase > ~0ULL - bytes ||
            !X86_LAYOUT_RANGE(segment->VirtualBase, bytes) ||  // K1: either canonical half
            description->PhysicalBase != segment->PhysicalBase ||
            description->VirtualBase != segment->VirtualBase || description->Bytes != bytes ||
            description->Flags != segment->Flags || description->Reserved)
            return EFI_INVALID_PARAMETER;
        if (segment->PhysicalBase < stack_end && stack->PhysicalBase < segment->PhysicalBase + bytes)
            return EFI_LOAD_ERROR;
        for (previous = 0; previous < index; ++previous) {
            const ELF_STAGED_SEGMENT *other = &kernel->Pages.Segments[previous];
            if (segment->PhysicalBase < other->PhysicalBase + other->Pages * 4096 &&
                other->PhysicalBase < segment->PhysicalBase + bytes)
                return EFI_LOAD_ERROR;
        }
        status = boot_check_loader_range(map, segment->PhysicalBase, segment->Pages);
        if (status != EFI_SUCCESS)
            return status;
        total += bytes;
    }
    if (total != kernel->Pages.AllocatedBytes || total != kernel->Description.AllocatedBytes)
        return EFI_INVALID_PARAMETER;
    return boot_check_loader_range(map, stack->PhysicalBase, stack->Pages);
}

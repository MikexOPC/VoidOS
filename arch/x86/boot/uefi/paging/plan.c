#include "plan.h"
#include "lib/memory/address_ranges.h"

// ====== intervalli e permessi del profilo a quattro livelli ====== //

EFI_STATUS boot_validate_mapping_plan(const BOOT_MAPPING_PLAN *plan)
{
    UINT32 index, previous;
    UINT64 total = 0;
    int executable_entry = 0;
    if (!plan || plan->Count < 2 || plan->Count > BOOT_MAPPING_LIMIT ||
        plan->StackTop != X86_BOOT_STACK_TOP)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        UINT64 bytes;
        if (!mapping->PhysicalBase || (mapping->PhysicalBase & 4095) ||
            mapping->VirtualBase < 4096 || (mapping->VirtualBase & 4095) ||
            mapping->VirtualBase >= X86_BOOT_LOW_ADDRESS_LIMIT || !mapping->Pages ||
            mapping->Pages > ELF_MAX_MEMORY_BYTES / 4096 ||
            (mapping->Flags & ~7U) || !(mapping->Flags & 4) ||
            (mapping->Flags & 3) == 3)
            return EFI_INVALID_PARAMETER;
        bytes = mapping->Pages * 4096;
        if (mapping->PhysicalBase > ~0ULL - bytes ||
            bytes > X86_BOOT_LOW_ADDRESS_LIMIT - mapping->VirtualBase)
            return EFI_INVALID_PARAMETER;
        if (index + 1 == plan->Count) {
            if (mapping->VirtualBase != BOOT_STACK_VIRTUAL ||
                mapping->Pages != BOOT_STACK_PAGES || mapping->Flags != 6)
                return EFI_INVALID_PARAMETER;
        } else {
            UINT64 budget_bytes = bytes;
            if (mapping->VirtualBase == BOOT_INFO_VIRTUAL) {
                if (mapping->Pages != 1 || mapping->Flags != 4)
                    return EFI_INVALID_PARAMETER;
                budget_bytes = 0;
            }
            if (mapping->VirtualBase == BOOT_MAP_VIRTUAL) {
                if (bytes > X86_BOOT_MAP_MAX_BYTES || mapping->Flags != 4)
                    return EFI_INVALID_PARAMETER;
                budget_bytes = 0;
            } else if (os_address_ranges_overlap(mapping->VirtualBase, bytes,
                                                 BOOT_MAP_VIRTUAL, X86_BOOT_MAP_MAX_BYTES)) {
                return EFI_INVALID_PARAMETER;
            }
            if (budget_bytes > ELF_MAX_MEMORY_BYTES - total ||
                (mapping->VirtualBase < plan->StackTop &&
                 BOOT_STACK_GUARD < mapping->VirtualBase + bytes))
                return EFI_INVALID_PARAMETER;
            total += budget_bytes;
            if ((mapping->Flags & 1) && plan->Entry >= mapping->VirtualBase &&
                plan->Entry < mapping->VirtualBase + bytes)
                executable_entry = 1;
        }
        for (previous = 0; previous < index; ++previous) {
            const BOOT_MAPPING *other = &plan->Mappings[previous];
            UINT64 other_bytes = other->Pages * 4096;
            if (os_address_ranges_overlap(mapping->VirtualBase, bytes, other->VirtualBase, other_bytes) ||
                os_address_ranges_overlap(mapping->PhysicalBase, bytes, other->PhysicalBase, other_bytes))
                return EFI_INVALID_PARAMETER;
        }
    }
    return executable_entry ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

// ====== conversione del contratto ancora vivo in piano numerico ====== //

EFI_STATUS boot_plan_mappings(const BOOT_KERNEL_IMAGE *kernel,
                             const BOOT_KERNEL_STACK *stack, BOOT_MAPPING_PLAN *plan)
{
    UINTN byte;
    UINT32 index;
    UINT64 total = 0;
    EFI_STATUS status;
    if (!plan)
        return EFI_INVALID_PARAMETER;
    for (byte = 0; byte < sizeof(*plan); ++byte)
        ((UINT8 *)plan)[byte] = 0;
    if (!kernel || !stack || kernel->Description.Signature != BOOT_SEGMENTS_SIGNATURE ||
        kernel->Description.Version != BOOT_SEGMENTS_VERSION ||
        kernel->Description.Size != sizeof(kernel->Description) || kernel->Description.Reserved ||
        !kernel->Pages.Count || kernel->Pages.Count > ELF_MAX_LOAD_SEGMENTS ||
        kernel->Description.SegmentCount != kernel->Pages.Count ||
        stack->Pages != BOOT_STACK_PAGES || stack->PhysicalBase > ~0ULL - X86_BOOT_STACK_BYTES ||
        stack->Top != stack->PhysicalBase + X86_BOOT_STACK_BYTES)
        return EFI_INVALID_PARAMETER;
    plan->Entry = kernel->Description.VirtualEntry;
    plan->StackTop = X86_BOOT_STACK_TOP;
    plan->Count = kernel->Pages.Count + 1;
    for (index = 0; index < kernel->Pages.Count; ++index) {
        const ELF_STAGED_SEGMENT *segment = &kernel->Pages.Segments[index];
        const BOOT_SEGMENT *description = &kernel->Description.Segments[index];
        if (segment->Pages > ELF_MAX_MEMORY_BYTES / 4096 || description->Reserved ||
            description->PhysicalBase != segment->PhysicalBase ||
            description->VirtualBase != segment->VirtualBase ||
            description->Flags != segment->Flags || description->Bytes != segment->Pages * 4096)
            goto invalid;
        plan->Mappings[index].PhysicalBase = segment->PhysicalBase;
        plan->Mappings[index].VirtualBase = segment->VirtualBase;
        plan->Mappings[index].Pages = segment->Pages;
        plan->Mappings[index].Flags = segment->Flags;
        total += description->Bytes;
    }
    plan->Mappings[index].PhysicalBase = stack->PhysicalBase;
    plan->Mappings[index].VirtualBase = BOOT_STACK_VIRTUAL;
    plan->Mappings[index].Pages = stack->Pages;
    plan->Mappings[index].Flags = 6;
    status = boot_validate_mapping_plan(plan);
    if (status == EFI_SUCCESS && total == kernel->Pages.AllocatedBytes &&
        total == kernel->Description.AllocatedBytes)
        return EFI_SUCCESS;
invalid:
    for (byte = 0; byte < sizeof(*plan); ++byte)
        ((UINT8 *)plan)[byte] = 0;
    return EFI_INVALID_PARAMETER;
}

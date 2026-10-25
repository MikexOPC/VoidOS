#include "prepare.h"
#include "arch/x86/configs/layout.h"

// ====== invalidazione senza proprieta o rilascio delle risorse ====== //

void boot_invalidate_start_info(BOOT_START_INFO *info)
{
    UINTN byte;
    if (!info) return;
    for (byte = 0; byte < sizeof(*info); ++byte)
        ((UINT8 *)info)[byte] = 0;
}

// ====== shadow space e indirizzo di ingresso dopo la futura CALL ====== //

EFI_STATUS boot_plan_entry_stack(UINT64 base, UINT64 bytes, BOOT_ENTRY_STACK *stack)
{
    UINT64 top;
    if (!stack) return EFI_INVALID_PARAMETER;
    stack->BeforeCall = stack->AtEntry = stack->ShadowBytes = 0;
    // K1: the boot stack may sit in either canonical half (layout.h).
    if ((base & 4095) || (bytes & 4095) || !X86_LAYOUT_RANGE(base, bytes))
        return EFI_INVALID_PARAMETER;
    top = base + bytes;
    stack->BeforeCall = top - 32;
    stack->AtEntry = top - 40;
    stack->ShadowBytes = 32;
    return EFI_SUCCESS;
}

// ====== contratto preparato da piano e tabelle ancora possedute ====== //

EFI_STATUS boot_prepare_start_info(const BOOT_MAPPING_PLAN *plan,
                                  const BOOT_PAGE_TABLES *tables,
                                  const BOOT_PAGING_CAPABILITIES *capabilities,
                                  BOOT_START_INFO *info)
{
    UINT32 index, previous;
    UINT64 limit;
    BOOT_ENTRY_STACK stack;
    EFI_STATUS status;
    if (!info) return EFI_INVALID_PARAMETER;
    boot_invalidate_start_info(info);
    if (!capabilities || capabilities->Valid != 1 || capabilities->NxSupported != 1 ||
        capabilities->PhysicalBits < 32 || capabilities->PhysicalBits > 52 ||
        !tables || tables->Valid != 1 || !tables->Count || tables->Count > BOOT_TABLE_LIMIT ||
        tables->Root != tables->Pages[0]) return EFI_INVALID_PARAMETER;
    status = boot_validate_mapping_plan(plan);
    if (status != EFI_SUCCESS) return status;
    limit = 1ULL << capabilities->PhysicalBits;
    for (index = 0; index < plan->Count; ++index) {
        const BOOT_MAPPING *mapping = &plan->Mappings[index];
        if (mapping->PhysicalBase >= limit || mapping->Pages * 4096 > limit - mapping->PhysicalBase)
            return EFI_INVALID_PARAMETER;
    }
    for (index = 0; index < tables->Count; ++index) {
        UINT32 mapping_index;
        UINT64 page = tables->Pages[index];
        if (!page || (page & 4095) || page > limit - 4096) return EFI_INVALID_PARAMETER;
        if (plan->DescriptorBase && page < plan->DescriptorBase + X86_BOOT_TRANSITION_DATA_PAGES * 4096 &&
            plan->DescriptorBase < page + 4096) return EFI_INVALID_PARAMETER;
        for (previous = 0; previous < index; ++previous)
            if (page == tables->Pages[previous]) return EFI_INVALID_PARAMETER;
        for (mapping_index = 0; mapping_index < plan->Count; ++mapping_index) {
            const BOOT_MAPPING *mapping = &plan->Mappings[mapping_index];
            UINT64 end = mapping->PhysicalBase + mapping->Pages * 4096;
            if (page < end && mapping->PhysicalBase < page + 4096)
                return EFI_INVALID_PARAMETER;
        }
    }
    status = boot_plan_entry_stack(BOOT_STACK_VIRTUAL, BOOT_STACK_PAGES * 4096, &stack);
    if (status != EFI_SUCCESS || stack.BeforeCall + 32 != plan->StackTop)
        return EFI_INVALID_PARAMETER;
    info->Version = BOOT_START_VERSION;
    info->Size = sizeof(*info);
    info->RequiredFeatures = BOOT_START_FEATURES;
    info->PageTableRoot = tables->Root;
    info->StackBase = BOOT_STACK_VIRTUAL;
    info->StackBytes = BOOT_STACK_PAGES * 4096;
    info->KernelEntry = plan->Entry;
    info->Signature = BOOT_START_SIGNATURE;
    return EFI_SUCCESS;
}

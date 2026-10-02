#include "activation.h"
#include "capsule.h"
#include "cpu/paging_state.h"
#include "lib/memory/address_ranges.h"

// ====== preflight prima di ogni tentativo di uscita firmware ====== //

EFI_STATUS boot_prepare_activation(const BOOT_EXIT_HANDOFF *handoff)
{
    BOOT_PAGING_STATE state = {0};
    BOOT_ACTIVATION_CONTEXT draft, *context;
    const BOOT_DESCRIPTOR_STORAGE *owner;
    EFI_STATUS status;
    if (!handoff || !handoff->Descriptors) return EFI_INVALID_PARAMETER;
    owner = handoff->Descriptors;
    status = boot_validate_exit_handoff(handoff);
    if (status == EFI_SUCCESS) status = boot_read_paging_state(handoff->Capabilities, &state);
    if (status != EFI_SUCCESS) return status;
    if (!boot_activation_check_state(state.Cr4, handoff->Tables->Root, handoff->Capabilities->PhysicalBits))
        return EFI_UNSUPPORTED;
    draft.Root = handoff->Tables->Root;
    draft.KernelEntry = handoff->Kernel->Description.VirtualEntry;
    draft.StackTop = X86_BOOT_STACK_TOP;
    draft.InfoPhysical = handoff->Page->PhysicalBase;
    draft.InfoVirtual = BOOT_INFO_VIRTUAL;
    draft.Gdtr = owner->PhysicalBase + BOOT_DESCRIPTOR_OFFSET((BOOT_DESCRIPTOR_BUNDLE *)(UINTN)owner->PhysicalBase, Gdtr);
    draft.Idtr = owner->PhysicalBase + BOOT_DESCRIPTOR_OFFSET((BOOT_DESCRIPTOR_BUNDLE *)(UINTN)owner->PhysicalBase, Idtr);
    context = (BOOT_ACTIVATION_CONTEXT *)(UINTN)(owner->PhysicalBase + BOOT_ACTIVATION_CONTEXT_OFFSET);
    {
        const void *objects[] = {handoff, owner, handoff->Kernel, handoff->Stack, handoff->Capabilities,
                                 handoff->Tables, handoff->Page, handoff->Memory};
        const UINTN sizes[] = {sizeof(*handoff), sizeof(*owner), sizeof(*handoff->Kernel), sizeof(*handoff->Stack),
            sizeof(*handoff->Capabilities), sizeof(*handoff->Tables), sizeof(*handoff->Page), sizeof(*handoff->Memory)};
        UINT32 index;
        for (index = 0; index < sizeof(objects) / sizeof(objects[0]); ++index)
            if (os_address_ranges_overlap((UINT64)(UINTN)context, sizeof(*context),
                (UINT64)(UINTN)objects[index], sizes[index])) return EFI_INVALID_PARAMETER;
    }
    *context = draft;
    return EFI_SUCCESS;
}

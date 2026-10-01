#include "paging_capabilities.h"
#include "paging_state.h"
#include "arch/x86/lib/processor/paging_validation.h"

// ====== interrogazione dei soli leaf estesi disponibili ====== //

EFI_STATUS boot_query_paging_capabilities(BOOT_CPUID_QUERY query, BOOT_PAGING_CAPABILITIES *result)
{
    UINT32 registers[4];
    UINT32 maximum_leaf, features, bits;
    if (!result)
        return EFI_INVALID_PARAMETER;
    result->PhysicalBits = result->NxSupported = result->Valid = 0;
    if (!query)
        return EFI_INVALID_PARAMETER;
    query(0x80000000U, registers);
    maximum_leaf = registers[0];
    if (maximum_leaf < 0x80000008U)
        return EFI_UNSUPPORTED;
    query(0x80000001U, registers);
    features = registers[3];
    query(0x80000008U, registers);
    bits = x86_validate_paging_profile(maximum_leaf, features, registers[0]);
    if (!bits)
        return EFI_UNSUPPORTED;
    result->PhysicalBits = bits;
    result->NxSupported = 1;
    result->Valid = 1;
    return EFI_SUCCESS;
}

// ====== decodifica dei registri senza accesso alla root fisica ====== //

EFI_STATUS boot_decode_paging_state(const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGING_STATE *state)
{
    UINT32 flags;
    if (!state)
        return EFI_INVALID_PARAMETER;
    state->Root = 0;
    state->Flags = state->Valid = 0;
    if (!capabilities || capabilities->Valid != 1 || capabilities->NxSupported != 1 ||
        capabilities->PhysicalBits < 32 || capabilities->PhysicalBits > 52)
        return EFI_INVALID_PARAMETER;
    flags = x86_validate_paging_state(state->Cr0, state->Cr3, state->Cr4, state->Efer, capabilities->PhysicalBits);
    if (!(flags & X86_PAGING_VALID))
        return EFI_UNSUPPORTED;
    state->Root = state->Cr3 & X86_PAGING_ROOT_MASK;
    state->Flags = flags & (X86_PAGING_NXE | X86_PAGING_WP | X86_PAGING_PCID);
    state->Valid = 1;
    return EFI_SUCCESS;
}

EFI_STATUS boot_compare_paging_state(const BOOT_PAGING_STATE *before, const BOOT_PAGING_STATE *after)
{
    if (!before || !after || before->Valid != 1 || after->Valid != 1)
        return EFI_INVALID_PARAMETER;
    return before->Cr3 == after->Cr3 && before->Root == after->Root && before->Flags == after->Flags ?
           EFI_SUCCESS : EFI_DEVICE_ERROR;
}

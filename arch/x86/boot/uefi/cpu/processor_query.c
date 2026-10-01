#include "paging_capabilities.h"
#include "paging_state.h"
#include "arch/x86/lib/processor/processor_access.h"

// ====== adattamento della libreria CPUID allo status UEFI ====== //

EFI_STATUS boot_read_paging_capabilities(BOOT_PAGING_CAPABILITIES *result)
{
    return boot_query_paging_capabilities(x86_query_cpuid, result);
}

// ====== lettura privilegiata soltanto nell'ambiente UEFI x64 ====== //

EFI_STATUS boot_read_paging_state(const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGING_STATE *state)
{
    X86_PAGING_REGISTERS registers;
    if (!state)
        return EFI_INVALID_PARAMETER;
    state->Root = 0;
    state->Valid = state->Flags = 0;
    if (!capabilities || capabilities->Valid != 1 || capabilities->NxSupported != 1 ||
        capabilities->PhysicalBits < 32 || capabilities->PhysicalBits > 52)
        return EFI_INVALID_PARAMETER;
    if (!x86_read_paging_registers(&registers))
        return EFI_DEVICE_ERROR;
    state->Cr0 = registers.Cr0;
    state->Cr3 = registers.Cr3;
    state->Cr4 = registers.Cr4;
    state->Efer = registers.Efer;
    return boot_decode_paging_state(capabilities, state);
}

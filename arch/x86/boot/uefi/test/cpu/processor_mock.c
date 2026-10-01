#include "cpu/paging_capabilities.h"
#include "cpu/paging_state.h"
#include "test/storage/kernel_memory_mock.h"

// ====== capacita simulate per indirizzi del processo Windows ====== //

static void query(UINT32 leaf, UINT32 registers[4])
{
    UINT32 index;
    for (index = 0; index < 4; ++index)
        registers[index] = 0;
    if (leaf == 0x80000000U)
        registers[0] = 0x80000008U;
    if (leaf == 0x80000001U)
        registers[3] = (1U << 29) | (kernel_mock_scenario == 33 ? 0U : (1U << 20));
    if (leaf == 0x80000008U)
        registers[0] = (48U << 8) | 52U;
}

EFI_STATUS boot_read_paging_capabilities(BOOT_PAGING_CAPABILITIES *result)
{
    return boot_query_paging_capabilities(query, result);
}

// ====== registri simulati senza istruzioni privilegiate nei test host ====== //

EFI_STATUS boot_read_paging_state(const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGING_STATE *state)
{
    if (!state)
        return EFI_INVALID_PARAMETER;
    state->Cr0 = (1ULL << 31) | (1ULL << 16) | 1;
    state->Cr3 = kernel_mock_scenario == 39 && kernel_mock_page_allocations > 3 ? 0x801000 : 0x800000;
    state->Cr4 = (1ULL << 5) | (kernel_mock_scenario == 38 ? (1ULL << 12) : 0);
    state->Efer = (1ULL << 8) | (1ULL << 10) | (1ULL << 11);
    return boot_decode_paging_state(capabilities, state);
}

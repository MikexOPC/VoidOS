#include "cpu/paging_capabilities.h"

static UINT32 scenario, calls, wrong_leaf;

// ====== CPUID simulato e controllo dell'ordine dei leaf ====== //

static void query(UINT32 leaf, UINT32 registers[4])
{
    UINT32 index;
    const UINT32 expected[3] = {0x80000000U, 0x80000001U, 0x80000008U};
    if (calls >= 3 || leaf != expected[calls])
        wrong_leaf = 1;
    ++calls;
    for (index = 0; index < 4; ++index)
        registers[index] = 0;
    if (leaf == 0x80000000U)
        registers[0] = scenario == 1 ? 0x80000007U : 0x80000008U;
    if (leaf == 0x80000001U)
        registers[3] = scenario == 2 ? (1U << 29) : (1U << 29) | (1U << 20);
    if (leaf == 0x80000008U)
        registers[0] = (48U << 8) | (scenario == 3 ? 53U : 46U);
}

// ====== pubblicazione solo dopo la verifica completa ====== //

__declspec(dllexport) int run_cpu_tests(void)
{
    BOOT_PAGING_CAPABILITIES result;
    for (scenario = 0; scenario < 4; ++scenario) {
        EFI_STATUS status;
        calls = wrong_leaf = 0;
        result.PhysicalBits = result.NxSupported = result.Valid = 99;
        status = boot_query_paging_capabilities(query, &result);
        if (wrong_leaf || calls != (scenario == 1 ? 1U : 3U))
            return 10 + (int)scenario;
        if (!scenario) {
            if (status != EFI_SUCCESS || result.PhysicalBits != 46 || result.NxSupported != 1 || result.Valid != 1)
                return 20;
        } else if (status != EFI_UNSUPPORTED || result.PhysicalBits || result.NxSupported || result.Valid)
            return 20 + (int)scenario;
    }
    calls = 0;
    if (boot_query_paging_capabilities(query, 0) != EFI_INVALID_PARAMETER || calls ||
        boot_query_paging_capabilities(0, &result) != EFI_INVALID_PARAMETER ||
        result.PhysicalBits || result.NxSupported || result.Valid)
        return 30;
    return 0;
}

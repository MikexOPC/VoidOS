#include "memory/regions.h"

static UINT8 bytes[3 * 48 + 1];
static BOOT_MEMORY_MAP map;

// ====== fixture non allineata e passo esteso ====== //

static void put_integer(UINTN offset, UINT64 value, UINTN length)
{
    UINTN index;
    for (index = 0; index < length; ++index)
        bytes[1 + offset + index] = (UINT8)(value >> (index * 8));
}

static void prepare(void)
{
    UINTN index;
    for (index = 0; index < sizeof(bytes); ++index)
        bytes[index] = 0;
    map.Buffer = bytes + 1;
    map.Capacity = map.Size = 3 * 48;
    map.DescriptorSize = 48;
    map.DescriptorVersion = map.Valid = 1;
    map.Key = 0;
    for (index = 0; index < 3; ++index) {
        put_integer(index * 48, index ? 6 : 7, 4);
        put_integer(index * 48 + 8, 0x100000 + index * 0x10000, 8);
        put_integer(index * 48 + 24, index + 1, 8);
        put_integer(index * 48 + 32, index ? (1ULL << 63) : 8, 8);
        put_integer(index * 48 + 40, ~0ULL, 8);
    }
}

// ====== classificazione conservativa e campi copiati ====== //

__declspec(dllexport) int run_region_tests(void)
{
    BOOT_MEMORY_REGION region;
    BOOT_MEMORY_SUMMARY summary;
    UINTN index;
    UINT32 type;
    prepare();
    if (boot_summarize_memory(&map, &summary) != EFI_SUCCESS || summary.RegionCount != 3 ||
        summary.ConventionalCandidatePages != 1 || summary.ExcludedPages != 5)
        return 801;
    if (boot_read_memory_region(&map, 0, &region) != EFI_SUCCESS || region.PhysicalBase != 0x100000 ||
        region.Pages != 1 || region.Attributes != 8 || region.FirmwareType != 7 || !region.ConventionalCandidate)
        return 802;
    for (type = 0; type <= 16; ++type) {
        prepare();
        put_integer(0, type, 4);
        if (boot_summarize_memory(&map, &summary) != EFI_SUCCESS ||
            summary.ConventionalCandidatePages != (type == 7 ? 1ULL : 0ULL) ||
            summary.ExcludedPages != (type == 7 ? 5ULL : 6ULL))
            return 803;
    }
    prepare();
    put_integer(0, 0xffffffff, 4);
    if (boot_read_memory_region(&map, 0, &region) != EFI_SUCCESS ||
        region.FirmwareType != 0xffffffff || region.ConventionalCandidate)
        return 804;
    for (index = 0; index < 64; ++index) {
        prepare();
        put_integer(32, 1ULL << index, 8);
        if (boot_read_memory_region(&map, 0, &region) != EFI_SUCCESS ||
            region.ConventionalCandidate != (index < 5 || index == 14 ? 1U : 0U))
            return 805;
    }
    prepare();
    if (boot_read_memory_region(&map, 3, &region) != EFI_INVALID_PARAMETER || region.Pages ||
        region.PhysicalBase || region.Attributes || region.FirmwareType || region.ConventionalCandidate)
        return 806;
    map.Valid = 0;
    if (boot_read_memory_region(&map, 0, &region) != EFI_INVALID_PARAMETER ||
        boot_summarize_memory(&map, &summary) != EFI_INVALID_PARAMETER || summary.RegionCount ||
        summary.ConventionalCandidatePages || summary.ExcludedPages)
        return 807;
    prepare();
    put_integer(48 + 8, 0x100000, 8);
    if (boot_summarize_memory(&map, &summary) != EFI_LOAD_ERROR || summary.RegionCount ||
        summary.ConventionalCandidatePages || summary.ExcludedPages)
        return 808;
    prepare();
    put_integer(24, ~0ULL, 8);
    if (boot_read_memory_region(&map, 0, &region) != EFI_LOAD_ERROR || region.Pages ||
        boot_summarize_memory(&map, &summary) != EFI_LOAD_ERROR || summary.RegionCount)
        return 809;
    prepare();
    map.Buffer = 0;
    if (boot_read_memory_region(&map, 0, &region) != EFI_INVALID_PARAMETER ||
        boot_summarize_memory(&map, &summary) != EFI_INVALID_PARAMETER)
        return 810;
    prepare();
    if (boot_read_memory_region(&map, 0, &region) != EFI_SUCCESS)
        return 811;
    map.Valid = 0;
    map.Buffer = 0;
    if (region.PhysicalBase != 0x100000 || region.Pages != 1 || region.Attributes != 8)
        return 812;
    if (boot_read_memory_region(0, 0, &region) != EFI_INVALID_PARAMETER ||
        boot_read_memory_region(&map, 0, 0) != EFI_INVALID_PARAMETER ||
        boot_summarize_memory(0, &summary) != EFI_INVALID_PARAMETER ||
        boot_summarize_memory(&map, 0) != EFI_INVALID_PARAMETER)
        return 813;
    return 0;
}

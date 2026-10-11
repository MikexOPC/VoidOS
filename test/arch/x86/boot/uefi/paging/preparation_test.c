#include "paging/preparation.h"

// ====== descrittori numerici senza accessi alla memoria fisica ====== //

static void write64(UINT8 *destination, UINT64 value)
{
    UINTN byte;
    for (byte = 0; byte < 8; ++byte)
        destination[byte] = (UINT8)(value >> (byte * 8));
}

__declspec(dllexport) int run_preparation_tests(void)
{
    UINT8 descriptors[96] = {0};
    BOOT_MEMORY_MAP map = {0};
    BOOT_PAGE_TABLES tables = {0};
    UINT32 scenario;
    descriptors[0] = descriptors[48] = 2;
    write64(descriptors + 8, 0x400000);
    write64(descriptors + 24, 1);
    write64(descriptors + 56, 0x800000);
    write64(descriptors + 72, 1);
    map.Buffer = descriptors;
    map.Capacity = map.Size = sizeof(descriptors);
    map.DescriptorSize = 48;
    map.DescriptorVersion = map.Valid = 1;
    tables.Root = tables.Pages[0] = 0x400000;
    tables.Pages[1] = 0x800000;
    tables.Count = 2;
    tables.Valid = 1;
    if (boot_check_table_resources(&map, &tables, 32) != EFI_SUCCESS)
        return 1;
    for (scenario = 0; scenario < 9; ++scenario) {
        BOOT_PAGE_TABLES bad = tables;
        switch (scenario) {
        case 0: bad.Valid = 0; break;
        case 1: bad.Count = 0; break;
        case 2: bad.Count = BOOT_TABLE_LIMIT + 1; break;
        case 3: bad.Root++; break;
        case 4: bad.Pages[1]++; break;
        case 5: bad.Pages[1] = bad.Pages[0]; break;
        case 6: bad.Pages[1] = 1ULL << 32; break;
        case 7: bad.Pages[1] = 0; break;
        default: bad.Pages[1] = 0x900000; break;
        }
        if (boot_check_table_resources(&map, &bad, 32) == EFI_SUCCESS)
            return 10 + (int)scenario;
    }
    descriptors[48] = 7;
    if (boot_check_table_resources(&map, &tables, 32) != EFI_LOAD_ERROR)
        return 30;
    descriptors[48] = 2;
    write64(descriptors + 80, 1ULL << 63);
    if (boot_check_table_resources(&map, &tables, 32) != EFI_LOAD_ERROR)
        return 31;
    write64(descriptors + 80, 0);
    if (boot_check_table_resources(&map, &tables, 53) != EFI_INVALID_PARAMETER)
        return 32;
    map.Valid = 0;
    if (boot_check_table_resources(&map, &tables, 32) != EFI_INVALID_PARAMETER)
        return 33;
    return 0;
}

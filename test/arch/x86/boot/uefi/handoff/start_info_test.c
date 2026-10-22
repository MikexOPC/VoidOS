#include "handoff/protocol/prepare.h"

// ====== preparazione numerica senza dereferenziare pagine fisiche ====== //

__declspec(dllexport) int run_start_info_tests(void)
{
    BOOT_MAPPING_PLAN plan = {0};
    BOOT_PAGE_TABLES tables = {0};
    BOOT_PAGING_CAPABILITIES capabilities = {32, 1, 1};
    static BOOT_START_INFO info;
    BOOT_ENTRY_STACK stack;
    UINT32 scenario;
    UINTN byte;
    plan.Entry = 0x100100;
    plan.StackTop = BOOT_STACK_VIRTUAL + 65536;
    plan.Count = 2;
    plan.Mappings[0].PhysicalBase = 0x400000;
    plan.Mappings[0].VirtualBase = 0x100000;
    plan.Mappings[0].Pages = 1;
    plan.Mappings[0].Flags = 5;
    plan.Mappings[1].PhysicalBase = 0x800000;
    plan.Mappings[1].VirtualBase = BOOT_STACK_VIRTUAL;
    plan.Mappings[1].Pages = 16;
    plan.Mappings[1].Flags = 6;
    tables.Root = tables.Pages[0] = 0xa00000;
    tables.Pages[1] = 0xa01000;
    tables.Count = 2;
    tables.Valid = 1;
    if (boot_prepare_start_info(&plan, &tables, &capabilities, &info) != EFI_SUCCESS ||
        info.Signature != BOOT_START_SIGNATURE || info.Version != BOOT_START_VERSION || info.Size != sizeof(info) ||
        info.RequiredFeatures != 3 || info.EnabledFeatures || info.Reserved ||
        info.PageTableRoot != tables.Root || info.KernelEntry != plan.Entry ||
        info.StackBase != BOOT_STACK_VIRTUAL || info.StackBytes != 65536) return 1;
    for (scenario = 0; scenario < 17; ++scenario) {
        BOOT_MAPPING_PLAN bad_plan = plan;
        BOOT_PAGE_TABLES bad_tables = tables;
        BOOT_PAGING_CAPABILITIES bad_caps = capabilities;
        switch (scenario) {
        case 0: bad_tables.Valid = 0; break;
        case 1: bad_tables.Count = BOOT_TABLE_LIMIT + 1; break;
        case 2: bad_tables.Root++; break;
        case 3: bad_tables.Pages[1] = bad_tables.Pages[0]; break;
        case 4: bad_tables.Pages[1] = 0x400000; break;
        case 5: bad_tables.Pages[1] = 0x80f000; break;
        case 6: bad_tables.Pages[1] = 1ULL << 32; break;
        case 7: bad_caps.NxSupported = 0; break;
        case 8: bad_caps.PhysicalBits = 53; break;
        case 9: bad_plan.Mappings[0].PhysicalBase = 1ULL << 32; break;
        case 10: bad_plan.Mappings[0].Flags = 7; break;
        case 11: bad_plan.Entry = 0; break;
        case 12: bad_tables.Count = 0; break;
        case 13: bad_tables.Pages[1]++; break;
        case 14: bad_tables.Root = bad_tables.Pages[0] = 0; break;
        case 15: bad_caps.Valid = 0; break;
        default: bad_caps.PhysicalBits = 31; break;
        }
        for (byte = 0; byte < sizeof(info); ++byte) ((UINT8 *)&info)[byte] = 0xff;
        if (boot_prepare_start_info(&bad_plan, &bad_tables, &bad_caps, &info) == EFI_SUCCESS ||
            info.Signature || info.PageTableRoot || info.EnabledFeatures || info.KernelEntry)
            return 10 + (int)scenario;
        for (byte = 0; byte < sizeof(info); ++byte)
            if (((UINT8 *)&info)[byte]) return 60 + (int)scenario;
    }
    if (boot_plan_entry_stack(BOOT_STACK_VIRTUAL, 65536, &stack) != EFI_SUCCESS ||
        stack.BeforeCall != plan.StackTop - 32 || stack.AtEntry != plan.StackTop - 40 ||
        stack.ShadowBytes != 32 || (stack.BeforeCall & 15) || (stack.AtEntry & 15) != 8)
        return 30;
    for (scenario = 0; scenario < 6; ++scenario) {
        UINT64 base = BOOT_STACK_VIRTUAL, bytes = 65536;
        switch (scenario) {
        case 0: base = 0; break;
        case 1: base++; break;
        case 2: bytes = 0; break;
        case 3: bytes = 39; break;
        case 4: base = 1ULL << 47; break;
        default: bytes = ~0ULL; break;
        }
        if (boot_plan_entry_stack(base, bytes, &stack) == EFI_SUCCESS ||
            stack.BeforeCall || stack.AtEntry || stack.ShadowBytes) return 40 + (int)scenario;
    }
    if (boot_prepare_start_info(0, &tables, &capabilities, &info) == EFI_SUCCESS || info.Signature ||
        boot_plan_entry_stack(4096, 4096, 0) != EFI_INVALID_PARAMETER) return 50;
    if (boot_plan_entry_stack(4096, 4096, &stack) != EFI_SUCCESS || stack.AtEntry != 8192 - 40 ||
        boot_prepare_start_info(&plan, &tables, &capabilities, 0) != EFI_INVALID_PARAMETER) return 51;
    boot_invalidate_start_info(0);
    return 0;
}

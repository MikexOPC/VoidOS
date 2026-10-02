#include "paging/tables.h"
#include "paging/verification/contents.h"

__declspec(align(4096)) static UINT64 arena[BOOT_TABLE_LIMIT][512];
static UINT32 allocations, frees, fail_at, fail_free, duplicate;

// ====== firmware simulato con pagine realmente accessibili al test ====== //

static EFI_STATUS EFIAPI allocate(UINT32 mode, UINT32 type, UINTN pages, UINT64 *address)
{
    if (mode || type != 2 || pages != 1 || allocations == fail_at || allocations == BOOT_TABLE_LIMIT)
        return EFI_OUT_OF_RESOURCES;
    *address = (UINT64)(UINTN)arena[duplicate && allocations ? 0 : allocations];
    ++allocations;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(UINT64 address, UINTN pages)
{
    UINT32 index;
    if (pages != 1)
        return EFI_INVALID_PARAMETER;
    for (index = 0; index < allocations; ++index) {
        if (address == (UINT64)(UINTN)arena[index]) {
            if (fail_free && index == 1)
                return EFI_DEVICE_ERROR;
            ++frees;
            return EFI_SUCCESS;
        }
    }
    return EFI_INVALID_PARAMETER;
}

// ====== lettore indipendente dei quattro livelli costruiti ====== //

static UINT64 inspect(UINT64 root, UINT64 virtual_address)
{
    const UINT32 shifts[4] = {39, 30, 21, 12};
    UINT32 level;
    for (level = 0; level < 4; ++level) {
        UINT64 *table = (UINT64 *)(UINTN)root;
        UINT64 entry = table[(virtual_address >> shifts[level]) & 511];
        if (!(entry & 1))
            return 0;
        if (level == 3)
            return entry;
        if ((entry & 4095) != 3 || (entry & BOOT_PTE_NX))
            return ~0ULL;
        root = entry & 0x000ffffffffff000ULL;
        if (root < (UINT64)(UINTN)arena ||
            root >= (UINT64)(UINTN)arena + sizeof(arena))
            return ~0ULL;
    }
    return ~0ULL;
}

// ====== permessi limiti e proprieta dopo errori parziali ====== //

__declspec(dllexport) int run_table_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MAPPING_PLAN plan = {0};
    BOOT_PAGE_TABLES tables = {0};
    UINT32 scenario;
    services.AllocatePages = allocate;
    services.FreePages = release;
    plan.Count = 4;
    plan.Entry = 0x1ff000;
    plan.StackTop = BOOT_STACK_VIRTUAL + 65536;
    plan.Mappings[0].PhysicalBase = 0x400000;
    plan.Mappings[0].VirtualBase = 0x1ff000;
    plan.Mappings[0].Pages = 2;
    plan.Mappings[0].Flags = 5;
    plan.Mappings[1].PhysicalBase = 0x500000;
    plan.Mappings[1].VirtualBase = 0x3ffff000;
    plan.Mappings[1].Pages = 2;
    plan.Mappings[1].Flags = 6;
    plan.Mappings[2].PhysicalBase = 0x600000;
    plan.Mappings[2].VirtualBase = (1ULL << 39) - 4096;
    plan.Mappings[2].Pages = 2;
    plan.Mappings[2].Flags = 4;
    plan.Mappings[3].PhysicalBase = 0x800000;
    plan.Mappings[3].VirtualBase = BOOT_STACK_VIRTUAL;
    plan.Mappings[3].Pages = 16;
    plan.Mappings[3].Flags = 6;
    allocations = frees = duplicate = fail_free = 0;
    fail_at = ~0U;
    if (boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_SUCCESS || !tables.Valid)
        return 1;
    for (scenario = 0; scenario < plan.Count; ++scenario) {
        UINT64 page;
        const BOOT_MAPPING *mapping = &plan.Mappings[scenario];
        for (page = 0; page < mapping->Pages; ++page) {
            UINT64 expected = mapping->PhysicalBase + page * 4096;
            expected |= 1 | ((mapping->Flags & 2) ? 2ULL : 0ULL) |
                        ((mapping->Flags & 1) ? 0ULL : BOOT_PTE_NX);
            if (inspect(tables.Root, mapping->VirtualBase + page * 4096) != expected)
                return 2;
        }
    }
    if (inspect(tables.Root, 0) || inspect(tables.Root, BOOT_STACK_GUARD) ||
        inspect(tables.Root, plan.StackTop) || inspect(tables.Root, 0x400000))
        return 3;
    if (boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_INVALID_PARAMETER)
        return 4;
    fail_free = 1;
    if (boot_release_page_tables(&services, &tables) != EFI_DEVICE_ERROR || tables.Root || tables.Valid ||
        frees != allocations - 1 || !tables.Pages[1])
        return 5;
    fail_free = 0;
    if (boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != allocations || tables.Count ||
        boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != allocations)
        return 6;
    for (scenario = 0; scenario < 16; ++scenario) {
        allocations = frees = 0;
        fail_at = scenario;
        if (boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_OUT_OF_RESOURCES ||
            tables.Valid || tables.Root || tables.Count != allocations)
            return 20 + (int)scenario;
        if (boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != allocations)
            return 40 + (int)scenario;
    }
    fail_at = ~0U;
    allocations = frees = 0;
    if (boot_build_page_tables(&services, &plan, 52, 0, &tables) != EFI_UNSUPPORTED || allocations ||
        boot_build_page_tables(&services, &plan, 53, 1, &tables) != EFI_UNSUPPORTED || allocations)
        return 60;
    duplicate = 1;
    if (boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_DEVICE_ERROR || tables.Count != 1 ||
        boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != 1)
        return 61;
    duplicate = 0;
    allocations = frees = 0;
    plan.Mappings[0].PhysicalBase = 1ULL << 32;
    if (boot_build_page_tables(&services, &plan, 32, 1, &tables) != EFI_UNSUPPORTED || allocations)
        return 62;
    plan.Mappings[0].PhysicalBase = 0x400000;
    if (boot_build_page_tables(&services, &plan, 32, 1, &tables) != EFI_DEVICE_ERROR || tables.Valid ||
        tables.Count != 1 || boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != 1)
        return 63;
    allocations = frees = 0;
    plan.Count = ELF_MAX_LOAD_SEGMENTS + 2;
    plan.Entry = 0x1ff000;
    for (scenario = 0; scenario < ELF_MAX_LOAD_SEGMENTS; ++scenario) {
        plan.Mappings[scenario].PhysicalBase = 0x10000000ULL + scenario * 0x800000ULL;
        plan.Mappings[scenario].VirtualBase = 0x1ff000 + (UINT64)scenario * (1ULL << 39);
        plan.Mappings[scenario].Pages = 2048;
        plan.Mappings[scenario].Flags = scenario ? 6U : 5U;
    }
    plan.Mappings[scenario].PhysicalBase = 0x21000000;
    plan.Mappings[scenario].VirtualBase = BOOT_INFO_VIRTUAL;
    plan.Mappings[scenario].Pages = 1;
    plan.Mappings[scenario].Flags = 4;
    ++scenario;
    plan.Mappings[scenario].PhysicalBase = 0x20000000;
    plan.Mappings[scenario].VirtualBase = BOOT_STACK_VIRTUAL;
    plan.Mappings[scenario].Pages = 16;
    plan.Mappings[scenario].Flags = 6;
    if (boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_SUCCESS ||
        boot_verify_table_contents(&plan, &tables, 52) != EFI_SUCCESS ||
        allocations > BOOT_TABLE_LIMIT || allocations < 100 ||
        inspect(tables.Root, BOOT_INFO_VIRTUAL) != (0x21000000ULL | 1 | BOOT_PTE_NX) ||
        inspect(tables.Root, plan.Mappings[15].VirtualBase + 2047 * 4096) !=
            (plan.Mappings[15].PhysicalBase + 2047 * 4096 + 3 + BOOT_PTE_NX) ||
        boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != allocations)
        return 64;
    return 0;
}

// ====== posizione di una voce nella fixture valida prima delle mutazioni ====== //

static UINT64 *entry_slot(UINT64 root, UINT64 virtual_address, UINT32 target)
{
    const UINT32 shifts[4] = {39, 30, 21, 12};
    UINT32 level;
    for (level = 0; level < 4; ++level) {
        UINT64 *slot = &((UINT64 *)(UINTN)root)[(virtual_address >> shifts[level]) & 511];
        if (level == target) return slot;
        if (!(*slot & 1)) return 0;
        root = *slot & 0x000ffffffffff000ULL;
    }
    return 0;
}

// ====== verifica delle tabelle e rifiuto delle strutture corrotte ====== //

__declspec(dllexport) int run_table_contents_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MAPPING_PLAN plan = {0};
    UINT32 scenario;
    services.AllocatePages = allocate;
    services.FreePages = release;
    plan.Count = 3;
    plan.Entry = 0x100100;
    plan.StackTop = BOOT_STACK_VIRTUAL + 65536;
    plan.Mappings[0].PhysicalBase = 0x400000;
    plan.Mappings[0].VirtualBase = 0x100000;
    plan.Mappings[0].Pages = 1;
    plan.Mappings[0].Flags = 5;
    plan.Mappings[1].PhysicalBase = 0x600000;
    plan.Mappings[1].VirtualBase = BOOT_INFO_VIRTUAL;
    plan.Mappings[1].Pages = 1;
    plan.Mappings[1].Flags = 4;
    plan.Mappings[2].PhysicalBase = 0x800000;
    plan.Mappings[2].VirtualBase = BOOT_STACK_VIRTUAL;
    plan.Mappings[2].Pages = 16;
    plan.Mappings[2].Flags = 6;
    for (scenario = 0; scenario < 24; ++scenario) {
        BOOT_PAGE_TABLES tables = {0}, bad;
        UINT64 *leaf, *info_leaf, *parent, *root;
        EFI_STATUS expected;
        allocations = frees = duplicate = fail_free = 0;
        fail_at = ~0U;
        if (boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_SUCCESS ||
            boot_verify_table_contents(&plan, &tables, 52) != EFI_SUCCESS) return 700;
        bad = tables;
        leaf = entry_slot(tables.Root, plan.Entry, 3);
        info_leaf = entry_slot(tables.Root, BOOT_INFO_VIRTUAL, 3);
        parent = entry_slot(tables.Root, plan.Entry, 2);
        root = (UINT64 *)(UINTN)tables.Root;
        if (!leaf || !info_leaf || !parent) return 701;
        expected = scenario < 14 || scenario >= 20 ? EFI_DEVICE_ERROR : EFI_INVALID_PARAMETER;
        switch (scenario) {
        case 0: *leaf = 0; break;
        case 1: *leaf |= 2; break;
        case 2: *leaf |= BOOT_PTE_NX; break;
        case 3: *leaf |= 4; break;
        case 4: *leaf |= 1ULL << 8; break;
        case 5: *leaf ^= 4096; break;
        case 6: *info_leaf |= 2; break;
        case 7: *info_leaf &= ~BOOT_PTE_NX; break;
        case 8: *parent |= 1ULL << 7; break;
        case 9: *parent = 0x123000 | 3; break;
        case 10: root[0] = tables.Root | 3; break;
        case 11: root[256] = root[0]; break;
        case 12: root[1] = root[0]; break;
        case 13: *entry_slot(tables.Root, BOOT_STACK_GUARD, 3) = 0x900000 | 3 | BOOT_PTE_NX; break;
        case 14: bad.Pages[1] = bad.Pages[0]; break;
        case 15: bad.Pages[1]++; break;
        case 16: bad.Root++; break;
        case 17: bad.Valid = 0; break;
        case 18: bad.Count = BOOT_TABLE_LIMIT + 1; break;
        case 19: bad.Pages[1] = 1ULL << 52; break;
        case 20: root[X86_BOOT_RECURSIVE_SLOT] = 0; break;
        case 21: root[X86_BOOT_RECURSIVE_SLOT] &= ~BOOT_PTE_NX; break;
        case 22: root[X86_BOOT_RECURSIVE_SLOT] |= 4; break;
        default: root[X86_BOOT_RECURSIVE_SLOT] ^= 4096; break;
        }
        if (boot_verify_table_contents(&plan, &bad, 52) != expected || !tables.Valid ||
            tables.Root != tables.Pages[0]) return 710 + (int)scenario;
        if (boot_release_page_tables(&services, &tables) != EFI_SUCCESS || frees != allocations)
            return 740 + (int)scenario;
    }
    return 0;
}

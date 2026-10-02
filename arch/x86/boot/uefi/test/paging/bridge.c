#include "paging/tables.h"

// ====== confine di test con il codice C di produzione ====== //

UINT32 paging_test_layout(UINTN size, UINTN alignment, UINTN pages_offset)
{
    BOOT_PAGE_TABLES sample;
    return size == sizeof(BOOT_PAGE_TABLES) && alignment == _Alignof(BOOT_PAGE_TABLES) &&
           pages_offset == (UINTN)(void *)sample.Pages - (UINTN)(void *)&sample;
}

EFI_STATUS paging_test_build(BOOT_PAGE_TABLES *tables, EFI_ALLOCATE_PAGES allocate,
                             EFI_FREE_PAGES release, UINT32 bits, UINT32 nx)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MAPPING_PLAN plan = {0};
    services.AllocatePages = allocate;
    services.FreePages = release;
    plan.Count = 4;
    plan.Entry = 0x1ff000;
    plan.StackTop = BOOT_STACK_VIRTUAL + BOOT_STACK_PAGES * 4096;
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
    plan.Mappings[3].Pages = BOOT_STACK_PAGES;
    plan.Mappings[3].Flags = 6;
    return boot_build_page_tables(&services, &plan, bits, nx, tables);
}

// ====== rilascio con gli stessi callback e nessun allocatore aggiuntivo ====== //

EFI_STATUS paging_test_release(BOOT_PAGE_TABLES *tables, EFI_FREE_PAGES release)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    services.FreePages = release;
    return boot_release_page_tables(&services, tables);
}

#include "handoff/protocol/page.h"

__declspec(align(4096)) static UINT8 storage[4096];
static UINT32 mode, release_error, releases;
static UINT64 alias_address;

// ====== simulazione degli errori firmware senza accessi a indirizzi invalidi ====== //

static EFI_STATUS EFIAPI allocate_page(UINT32 type, UINT32 memory, UINTN count, UINT64 *address)
{
    if (type || memory != 2 || count != 1) return EFI_INVALID_PARAMETER;
    if (mode == 1) return EFI_OUT_OF_RESOURCES;
    *address = mode == 5 ? alias_address : mode == 2 ? (UINT64)(UINTN)storage + 1 :
               mode == 3 ? 1ULL << 52 : mode == 4 ? 0x400000 : (UINT64)(UINTN)storage;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release_page(UINT64 address, UINTN count)
{
    (void)address;
    if (count != 1) return EFI_INVALID_PARAMETER;
    ++releases;
    return release_error ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

// ====== proprieta azzeramento mappatura R NX e rollback ====== //

__declspec(dllexport) int run_start_page_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MAPPING_PLAN plan = {0}, original;
    BOOT_START_PAGE page = {0};
    UINTN byte;
    services.Hdr.HeaderSize = sizeof(services);
    services.AllocatePages = allocate_page;
    services.FreePages = release_page;
    plan.Count = 2;
    plan.Entry = 0x100100;
    plan.StackTop = BOOT_STACK_VIRTUAL + 65536;
    plan.Mappings[0].PhysicalBase = 0x400000;
    plan.Mappings[0].VirtualBase = 0x100000;
    plan.Mappings[0].Pages = 1;
    plan.Mappings[0].Flags = 5;
    plan.Mappings[1].PhysicalBase = 0x800000;
    plan.Mappings[1].VirtualBase = BOOT_STACK_VIRTUAL;
    plan.Mappings[1].Pages = 16;
    plan.Mappings[1].Flags = 6;
    original = plan;
    mode = release_error = releases = 0;
    for (byte = 0; byte < sizeof(storage); ++byte) storage[byte] = 0xff;
    if (boot_allocate_start_page(&services, &plan, 52, &page) != EFI_SUCCESS || page.Pages != 1 || page.Ready)
        return 1;
    for (byte = 0; byte < sizeof(storage); ++byte) if (storage[byte]) return 2;
    if (boot_map_start_page(&plan, &page) != EFI_SUCCESS || plan.Count != 3 ||
        plan.Mappings[1].VirtualBase != BOOT_INFO_VIRTUAL || plan.Mappings[1].Flags != 4 ||
        plan.Mappings[2].VirtualBase != BOOT_STACK_VIRTUAL) return 3;
    if (boot_map_start_page(&plan, &page) == EFI_SUCCESS || plan.Count != 3) return 4;
    page.Ready = 1;
    ((BOOT_START_INFO *)storage)->Signature = BOOT_START_SIGNATURE;
    release_error = 1;
    if (boot_release_start_page(&services, &page) != EFI_DEVICE_ERROR || page.Ready ||
        page.Pages != 1 || !page.PhysicalBase || ((BOOT_START_INFO *)storage)->Signature) return 5;
    release_error = 0;
    if (boot_release_start_page(&services, &page) != EFI_SUCCESS || page.Pages || page.PhysicalBase ||
        boot_release_start_page(&services, &page) != EFI_SUCCESS || releases != 2) return 6;
    for (mode = 1; mode <= 4; ++mode) {
        EFI_STATUS expected = mode == 1 ? EFI_OUT_OF_RESOURCES : EFI_DEVICE_ERROR;
        if (boot_allocate_start_page(&services, &original, 52, &page) != expected || page.Ready)
            return 10 + (int)mode;
        if (mode == 1 || mode == 4) {
            if (page.Pages || page.PhysicalBase) return 20 + (int)mode;
        } else if (page.Pages != 1 || boot_release_start_page(&services, &page) != EFI_SUCCESS)
            return 30 + (int)mode;
    }
    for (byte = 0; byte < 3; ++byte) {
        UINT32 previous_releases = releases;
        mode = 5;
        alias_address = byte == 0 ? (UINT64)(UINTN)&page & ~4095ULL :
            byte == 1 ? (UINT64)(UINTN)&original & ~4095ULL : (UINT64)(UINTN)&services & ~4095ULL;
        if (boot_allocate_start_page(&services, &original, 52, &page) != EFI_DEVICE_ERROR ||
            page.PhysicalBase || page.Pages || page.Ready || releases != previous_releases) return 45 + (int)byte;
    }
    plan = original;
    page.PhysicalBase = 0x400000;
    page.Pages = 1;
    if (boot_map_start_page(&plan, &page) == EFI_SUCCESS) return 40;
    for (byte = 0; byte < sizeof(plan); ++byte)
        if (((UINT8 *)&plan)[byte] != ((UINT8 *)&original)[byte]) return 41;
    return 0;
}

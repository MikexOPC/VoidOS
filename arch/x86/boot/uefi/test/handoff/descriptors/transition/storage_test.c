#include "handoff/descriptors/transition/storage.h"
#include "test/storage/kernel_memory_mock.h"
#include "handoff/firmware/terminal/handoff.h"
#include "handoff/descriptors/exceptions.h"

EFI_STATUS test_run_descriptor_scenario(int scenario);
UINT32 test_descriptor_phase(void);
static __declspec(align(4096)) UINT8 storage[9 * 4096];
static UINT32 releases;

// ====== mock di rilascio che conserva il proprietario sui primi due fallimenti ====== //

static EFI_STATUS EFIAPI release_pages(UINT64 base, UINTN pages)
{
    if (base != (UINT64)(UINTN)storage || pages != 9) return EFI_INVALID_PARAMETER;
    ++releases;
    return releases < 3 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

// ====== retry senza doppi rilasci e rollback del piano ====== //

static int check_owner(void)
{
    BOOT_DESCRIPTOR_STORAGE owner = {(UINT64)(UINTN)storage, 9, 1};
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MAPPING_PLAN plan = {0};
    UINT64 signature = BOOT_DESCRIPTOR_SIGNATURE;
    UINT32 byte;
    for (byte = 0; byte < 8; ++byte) storage[byte] = (UINT8)(signature >> (byte * 8));
    services.Hdr.HeaderSize = sizeof(services);
    services.FreePages = release_pages;
    releases = 0;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_DEVICE_ERROR || owner.Ready ||
        owner.Pages != 9 || owner.PhysicalBase != (UINT64)(UINTN)storage) return 1;
    for (byte = 0; byte < 8; ++byte) if (storage[byte]) return 2;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_DEVICE_ERROR || releases != 2 || owner.Pages != 9)
        return 3;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_SUCCESS || owner.Pages || owner.PhysicalBase)
        return 4;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_SUCCESS || releases != 3) return 5;
    owner.PhysicalBase = 0x800000;
    owner.Pages = 9;
    owner.Ready = 1;
    plan.Entry = 0x2000;
    plan.StackTop = X86_BOOT_STACK_TOP;
    plan.Count = 2;
    plan.Mappings[0].PhysicalBase = 0x100000;
    plan.Mappings[0].VirtualBase = 0x2000;
    plan.Mappings[0].Pages = 1;
    plan.Mappings[0].Flags = 5;
    plan.Mappings[1].PhysicalBase = 0x200000;
    plan.Mappings[1].VirtualBase = BOOT_STACK_VIRTUAL;
    plan.Mappings[1].Pages = BOOT_STACK_PAGES;
    plan.Mappings[1].Flags = 6;
    if (boot_map_descriptor_storage(&plan, &owner) != EFI_SUCCESS || plan.Count != 5 ||
        plan.Mappings[1].PhysicalBase != owner.PhysicalBase || plan.Mappings[1].Flags != 6 ||
        plan.Mappings[2].VirtualBase != X86_BOOT_PRIVILEGE_VIRTUAL ||
        plan.Mappings[3].VirtualBase != X86_BOOT_EXCEPTION_VIRTUAL ||
        plan.Mappings[4].VirtualBase != BOOT_STACK_VIRTUAL) return 6;
    if (boot_map_descriptor_storage(&plan, &owner) == EFI_SUCCESS || plan.Count != 5) return 7;
    plan.Mappings[2].VirtualBase = X86_BOOT_DESCRIPTOR_VIRTUAL + 4096;
    if (boot_validate_mapping_plan(&plan) == EFI_SUCCESS) return 8;
    return 0;
}

// ====== contenuto corrotto e stato terminale sovrapposto alle nuove pagine ====== //

static int check_bindings(void)
{
    BOOT_DESCRIPTOR_STORAGE owner = {(UINT64)(UINTN)storage, 9, 1};
    BOOT_DESCRIPTOR_LAYOUT layout;
    BOOT_DESCRIPTOR_BUNDLE *bundle = (BOOT_DESCRIPTOR_BUNDLE *)storage;
    BOOT_EXIT_HANDOFF handoff = {0};
    BOOT_EXIT_STATE *state = (BOOT_EXIT_STATE *)(storage + 4096);
    UINT32 index;
    layout.LinearBase = X86_BOOT_DESCRIPTOR_VIRTUAL;
    layout.PrivilegeStack = X86_BOOT_PRIVILEGE_VIRTUAL;
    layout.ExceptionStack = X86_BOOT_EXCEPTION_VIRTUAL;
    for (index = 0; index < 32; ++index) layout.Targets[index] = x86_boot_exception_targets[index];
    if (!boot_prepare_descriptor_bundle(bundle, &layout) || boot_check_descriptor_storage(&owner) != EFI_SUCCESS)
        return 1;
    bundle->Gdtr.Bytes[0] ^= 1;
    if (boot_check_descriptor_storage(&owner) != EFI_DEVICE_ERROR) return 2;
    bundle->Gdtr.Bytes[0] ^= 1;
    handoff.Descriptors = &owner;
    state->Phase = BOOT_SERVICES_AVAILABLE;
    state->Attempts = 0;
    state->LastStatus = 1234;
    if (boot_exit_memory_handoff(0, 0, &handoff, state) != EFI_INVALID_PARAMETER || state->LastStatus != 1234)
        return 3;
    owner.Ready = 2;
    if (boot_check_descriptor_storage(&owner) != EFI_INVALID_PARAMETER) return 4;
    owner.Ready = 0;
    owner.Pages = 8;
    if (boot_release_descriptor_storage(0, &owner) != EFI_INVALID_PARAMETER) return 5;
    /* Il metadata non puo vivere nelle pagine che il rilascio sta per liberare. */
    {
        BOOT_DESCRIPTOR_STORAGE *inside = (BOOT_DESCRIPTOR_STORAGE *)(storage + 8192);
        inside->PhysicalBase = (UINT64)(UINTN)storage;
        inside->Pages = 9;
        inside->Ready = 0;
        if (boot_release_descriptor_storage(0, inside) != EFI_INVALID_PARAMETER) return 6;
    }
    return 0;
}

// ====== intero ingresso EFI con errori di allocazione rebuild refresh e copertura ====== //

__declspec(dllexport) int run_descriptor_storage_tests(void)
{
    const int cases[] = {0, 60, 61, 62, 63, 64, 65, 67, 68, 69, 70, 71, 72};
    const EFI_STATUS expected[] = {EFI_SUCCESS, EFI_OUT_OF_RESOURCES, EFI_DEVICE_ERROR, EFI_DEVICE_ERROR,
        EFI_DEVICE_ERROR, EFI_DEVICE_ERROR, EFI_OUT_OF_RESOURCES, EFI_LOAD_ERROR, EFI_DEVICE_ERROR,
        EFI_DEVICE_ERROR, EFI_DEVICE_ERROR, EFI_DEVICE_ERROR, EFI_DEVICE_ERROR};
    UINT32 index;
    int result = check_owner();
    if (result) return 10 + result;
    result = check_bindings();
    if (result) return 30 + result;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        EFI_STATUS status = test_run_descriptor_scenario(cases[index]);
        if (status != expected[index]) return 10000 + cases[index] + 1000 * (int)(status & 255) +
            100000 * (int)test_descriptor_phase();
        if (kernel_mock_page_allocations != kernel_mock_page_frees) return 200 + cases[index];
        if (kernel_mock_descriptor_live() != (cases[index] == 68 ? 1U : 0U)) return 300 + cases[index];
    }
    return 0;
}

#include "handoff/descriptors/transition/storage.h"
#include "../../../storage/kernel_memory_mock.h"
#include "handoff/firmware/terminal/handoff.h"
#include "handoff/descriptors/exceptions.h"
#include "handoff/transition/capsule.h"

EFI_STATUS test_run_descriptor_scenario(int scenario);
UINT32 test_descriptor_phase(void);
static __declspec(align(4096)) UINT8 storage[11 * 4096];
static __declspec(align(4096)) UINT8 code[4096];
static UINT32 releases;
static UINT32 code_releases, data_releases;

// ====== due risorse con fallimenti indipendenti e nessun doppio rilascio ====== //

static EFI_STATUS EFIAPI release_pair(UINT64 base, UINTN pages)
{
    if (base == (UINT64)(UINTN)code && pages == 1)
        return ++code_releases == 1 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    if (base == (UINT64)(UINTN)storage && pages == 11)
        return ++data_releases == 1 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    return EFI_INVALID_PARAMETER;
}

static int check_pair(void)
{
    BOOT_DESCRIPTOR_STORAGE owner = {(UINT64)(UINTN)storage, 11, 1, (UINT64)(UINTN)code};
    EFI_BOOT_SERVICES_PREFIX services = {0};
    services.Hdr.HeaderSize = sizeof(services);
    services.FreePages = release_pair;
    code_releases = data_releases = 0;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_DEVICE_ERROR ||
        !owner.CodeBase || owner.Pages != 11 || owner.Ready || data_releases) return 1;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_DEVICE_ERROR ||
        owner.CodeBase || owner.Pages != 11 || code_releases != 2 || data_releases != 1) return 2;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_SUCCESS || owner.PhysicalBase || owner.Pages)
        return 3;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_SUCCESS || code_releases != 2 || data_releases != 2)
        return 4;
    return 0;
}

// ====== mock di rilascio che conserva il proprietario sui primi due fallimenti ====== //

static EFI_STATUS EFIAPI release_pages(UINT64 base, UINTN pages)
{
    if (base != (UINT64)(UINTN)storage || pages != 11) return EFI_INVALID_PARAMETER;
    ++releases;
    return releases < 3 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

// ====== retry senza doppi rilasci e rollback del piano ====== //

static int check_owner(void)
{
    BOOT_DESCRIPTOR_STORAGE owner = {(UINT64)(UINTN)storage, 11, 1, 0};
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MAPPING_PLAN plan = {0};
    UINT64 signature = BOOT_DESCRIPTOR_SIGNATURE;
    UINT32 byte;
    for (byte = 0; byte < 8; ++byte) storage[byte] = (UINT8)(signature >> (byte * 8));
    services.Hdr.HeaderSize = sizeof(services);
    services.FreePages = release_pages;
    releases = 0;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_DEVICE_ERROR || owner.Ready ||
        owner.Pages != 11 || owner.PhysicalBase != (UINT64)(UINTN)storage) return 1;
    for (byte = 0; byte < 8; ++byte) if (storage[byte]) return 2;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_DEVICE_ERROR || releases != 2 || owner.Pages != 11)
        return 3;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_SUCCESS || owner.Pages || owner.PhysicalBase)
        return 4;
    if (boot_release_descriptor_storage(&services, &owner) != EFI_SUCCESS || releases != 3) return 5;
    owner.PhysicalBase = 0x800000;
    owner.CodeBase = 0x900000;
    owner.Pages = 11;
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
    if (boot_map_descriptor_storage(&plan, &owner) != EFI_SUCCESS || plan.Count != 6 ||
        plan.Mappings[1].PhysicalBase != owner.PhysicalBase || plan.Mappings[1].Flags != 6 ||
        plan.Mappings[2].VirtualBase != owner.PhysicalBase + 2 * 4096 ||
        plan.Mappings[3].VirtualBase != owner.PhysicalBase + 7 * 4096 ||
        plan.Mappings[4].VirtualBase != owner.CodeBase ||
        plan.Mappings[4].Flags != 5 || plan.Mappings[5].VirtualBase != BOOT_STACK_VIRTUAL) return 6;
    if (boot_map_descriptor_storage(&plan, &owner) == EFI_SUCCESS || plan.Count != 6) return 7;
    plan.Mappings[4].Flags = 7;
    if (boot_validate_mapping_plan(&plan) == EFI_SUCCESS) return 9;
    plan.Mappings[4].Flags = 5;
    plan.CapsuleBase += 4096;
    if (boot_validate_mapping_plan(&plan) == EFI_SUCCESS) return 10;
    plan.CapsuleBase -= 4096;
    {
        UINT64 entry = plan.Entry;
        plan.Entry = plan.CapsuleBase;
        if (boot_validate_mapping_plan(&plan) == EFI_SUCCESS) return 12;
        plan.Entry = entry;
    }
    plan.Mappings[2].VirtualBase = owner.PhysicalBase + 4096;
    if (boot_validate_mapping_plan(&plan) == EFI_SUCCESS) return 8;
    plan.Mappings[2].VirtualBase = owner.PhysicalBase + 2 * 4096;
    /* La pagina del codice non riduce il budget ELF di 128 MiB. */
    owner.PhysicalBase = 0x20000000;
    owner.CodeBase = 0x30000000;
    plan.Count = 2;
    plan.CapsuleBase = 0;
    plan.DescriptorBase = 0;
    plan.Mappings[0].Pages = ELF_MAX_MEMORY_BYTES / 4096;
    plan.Mappings[1] = plan.Mappings[5];
    plan.Mappings[1].PhysicalBase = 0x10000000;
    if (boot_map_descriptor_storage(&plan, &owner) != EFI_SUCCESS) return 11;
    return 0;
}

// ====== contenuto corrotto e stato terminale sovrapposto alle nuove pagine ====== //

static int check_bindings(void)
{
    BOOT_DESCRIPTOR_STORAGE owner = {(UINT64)(UINTN)storage, 11, 1, 0};
    BOOT_DESCRIPTOR_LAYOUT layout;
    BOOT_DESCRIPTOR_BUNDLE *bundle = (BOOT_DESCRIPTOR_BUNDLE *)storage;
    BOOT_EXIT_HANDOFF handoff = {0};
    BOOT_EXIT_STATE *state = (BOOT_EXIT_STATE *)(storage + 4096);
    UINT32 index;
    owner.CodeBase = (UINT64)(UINTN)code;
    layout.LinearBase = owner.PhysicalBase;
    layout.PrivilegeStack = owner.PhysicalBase + 2 * 4096;
    layout.ExceptionStack = owner.PhysicalBase + 7 * 4096;
    if (!boot_prepare_capsule(code, 4096)) return 7;
    for (index = 0; index < 32; ++index) layout.Targets[index] = owner.CodeBase +
        BOOT_CAPSULE_VECTOR_OFFSET + index * BOOT_CAPSULE_VECTOR_STRIDE;
    if (!boot_prepare_descriptor_bundle(bundle, &layout) || boot_check_descriptor_storage(&owner) != EFI_SUCCESS)
        return 1;
    bundle->Gdtr.Bytes[0] ^= 1;
    if (boot_check_descriptor_storage(&owner) != EFI_DEVICE_ERROR) return 2;
    bundle->Gdtr.Bytes[0] ^= 1;
    handoff.Descriptors = &owner;
    code[4095] ^= 1;
    if (boot_check_descriptor_storage(&owner) != EFI_DEVICE_ERROR) return 8;
    code[4095] ^= 1;
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
        inside->Pages = 11;
        inside->Ready = 0;
        if (boot_release_descriptor_storage(0, inside) != EFI_INVALID_PARAMETER) return 6;
    }
    return 0;
}

// ====== intero ingresso EFI con errori di allocazione rebuild refresh e copertura ====== //

__declspec(dllexport) int run_descriptor_storage_tests(void)
{
    const int cases[] = {0, 60, 61, 62, 63, 64, 65, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 78, 79};
    const EFI_STATUS expected[] = {EFI_SUCCESS, EFI_OUT_OF_RESOURCES, EFI_DEVICE_ERROR, EFI_DEVICE_ERROR,
        EFI_DEVICE_ERROR, EFI_DEVICE_ERROR, EFI_OUT_OF_RESOURCES, EFI_LOAD_ERROR, EFI_DEVICE_ERROR,
        EFI_DEVICE_ERROR, EFI_DEVICE_ERROR, EFI_DEVICE_ERROR, EFI_DEVICE_ERROR,
        EFI_DEVICE_ERROR, EFI_OUT_OF_RESOURCES, EFI_LOAD_ERROR, EFI_DEVICE_ERROR, EFI_LOAD_ERROR, EFI_UNSUPPORTED};
    UINT32 index;
    int result = check_owner();
    if (result) return 10 + result;
    result = check_pair();
    if (result) return 20 + result;
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

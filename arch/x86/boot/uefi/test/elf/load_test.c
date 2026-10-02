#include "elf/load.h"

void make_kernel_fixture(UINT8 *buffer);
__declspec(align(4096)) static UINT8 memory[2][8192];
static UINT8 fixture[512];
static UINTN calls, released, requested[2];
static int fail_allocation, fail_release, bad_mock, invalid_address;
static int alias_mode;
static UINT64 services_address;

// ====== allocazioni e rilasci controllati ====== //

static EFI_STATUS EFIAPI allocate(UINT32 mode, UINT32 type, UINTN pages, UINT64 *address)
{
    UINTN index = calls++;
    if (mode || type != 2 || !pages || pages > 2 || index >= 2) {
        bad_mock = 1;
        return EFI_INVALID_PARAMETER;
    }
    requested[index] = pages;
    if (fail_allocation && index == 1) {
        *address = 123;
        return EFI_OUT_OF_RESOURCES;
    }
    *address = (UINT64)memory[index] + (invalid_address && index == 1 ? 1ULL : 0ULL);
    if (index == 1 && alias_mode) {
        *address = alias_mode == 1 ? (UINT64)memory[0] :
            alias_mode == 2 ? (UINT64)memory[0] + 4096 :
            alias_mode == 3 ? (UINT64)memory[0] + 4095 :
            alias_mode == 4 ? (UINT64)fixture : services_address;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(UINT64 address, UINTN pages)
{
    UINTN index;
    ++released;
    for (index = 0; index < 2; ++index)
        if (address == (UINT64)memory[index] + (invalid_address && index == 1 ? 1ULL : 0ULL) &&
            pages == requested[index])
            return fail_release == 1 || (fail_release == 2 && index == 1) ?
                EFI_DEVICE_ERROR : EFI_SUCCESS;
    bad_mock = 1;
    return EFI_INVALID_PARAMETER;
}

// ====== fixture e memoria con sentinelle ====== //

static void prepare(EFI_BOOT_SERVICES_PREFIX *services, ELF_STAGED_IMAGE *image)
{
    UINTN segment, byte;
    calls = released = 0;
    fail_allocation = fail_release = bad_mock = invalid_address = 0;
    alias_mode = 0;
    services_address = (UINT64)(UINTN)services;
    image->Count = 0;
    image->AllocatedBytes = 0;
    services->Hdr.HeaderSize = sizeof(*services);
    services->AllocatePages = allocate;
    services->FreePages = release;
    make_kernel_fixture(fixture);
    for (byte = 0; byte < 16; ++byte)
        fixture[256 + byte] = (UINT8)(byte + 1);
    for (byte = 0; byte < 8; ++byte)
        fixture[384 + byte] = (UINT8)(byte + 33);
    for (segment = 0; segment < 2; ++segment)
        for (byte = 0; byte < 8192; ++byte)
            memory[segment][byte] = 0xcc;
}

// ====== copia, BSS, confini e conservazione della proprieta ====== //

__declspec(dllexport) int run_load_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services;
    ELF_STAGED_IMAGE image;
    UINTN segment, byte;
    if ((UINTN)((UINT8 *)&services.AllocatePages - (UINT8 *)&services) != 40 ||
        (UINTN)((UINT8 *)&services.FreePages - (UINT8 *)&services) != 48)
        return 500;
    prepare(&services, &image);
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_SUCCESS ||
        image.Count != 2 || image.AllocatedBytes != 8192 || calls != 2 || released || bad_mock)
        return 501;
    for (segment = 0; segment < 2; ++segment) {
        UINTN offset = segment ? 384 : 256;
        UINTN length = segment ? 8 : 16;
        if (image.Segments[segment].PhysicalBase != (UINT64)memory[segment] ||
            image.Segments[segment].VirtualBase != (segment ? 0x200000ULL : 0x100000ULL) ||
            image.Segments[segment].Flags != (segment ? 6U : 5U))
            return 502;
        for (byte = 0; byte < 8192; ++byte) {
            UINT8 expected = byte >= 4096 ? 0xcc : 0;
            if (byte >= offset && byte < offset + length)
                expected = fixture[byte];
            if (memory[segment][byte] != expected)
                return 503;
        }
    }
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_INVALID_PARAMETER || calls != 2)
        return 504;
    if (elf_release_segments(&services, &image) != EFI_SUCCESS || image.Count || image.AllocatedBytes || released != 2)
        return 505;
    if (elf_release_segments(&services, &image) != EFI_SUCCESS || released != 2)
        return 506;
    prepare(&services, &image);
    fail_allocation = 1;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_OUT_OF_RESOURCES ||
        image.Count || image.AllocatedBytes || released != 1 || bad_mock)
        return 507;
    prepare(&services, &image);
    fail_allocation = fail_release = 1;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_OUT_OF_RESOURCES ||
        image.Count != 1 || image.AllocatedBytes != 4096 || released != 1)
        return 508;
    fail_release = 0;
    if (elf_release_segments(&services, &image) != EFI_SUCCESS || image.Count || image.AllocatedBytes)
        return 509;
    prepare(&services, &image);
    fixture[0] = 0;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_LOAD_ERROR || calls)
        return 510;
    prepare(&services, &image);
    services.AllocatePages = 0;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_UNSUPPORTED || calls)
        return 511;
    prepare(&services, &image);
    fixture[104] = 0; fixture[105] = 16;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_SUCCESS ||
        requested[0] != 2 || image.AllocatedBytes != 12288)
        return 512;
    for (byte = 272; byte < 8192; ++byte)
        if (memory[0][byte])
            return 513;
    if (elf_release_segments(&services, &image) != EFI_SUCCESS || bad_mock)
        return 514;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), 0) != EFI_INVALID_PARAMETER)
        return 515;
    prepare(&services, &image);
    // ====== budget virtuale valido ma pagine arrotondate oltre il limite ====== //

    fixture[104] = 192; fixture[105] = 255; fixture[106] = 255; fixture[107] = 7;
    fixture[136] = 128; fixture[137] = 1; fixture[138] = 0; fixture[139] = 16;
    if (elf_validate(fixture, sizeof(fixture), &(ELF_LOAD_PLAN){0}) != ELF_VALID ||
        elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_OUT_OF_RESOURCES || calls)
        return 516;
    prepare(&services, &image);
    invalid_address = 1;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_DEVICE_ERROR ||
        released != 2 || image.Count || image.AllocatedBytes || bad_mock)
        return 517;
    for (byte = 0; byte < sizeof(memory[1]); ++byte)
        if (memory[1][byte] != 0xcc)
            return 518;
    prepare(&services, &image);
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_SUCCESS)
        return 519;
    fail_release = 2;
    if (elf_release_segments(&services, &image) != EFI_DEVICE_ERROR || image.Count != 2 ||
        image.AllocatedBytes != 4096 || image.Segments[0].Pages || image.Segments[1].Pages != 1)
        return 520;
    fail_release = 0;
    if (elf_release_segments(&services, &image) != EFI_SUCCESS || released != 3 ||
        image.Count || image.AllocatedBytes || bad_mock)
        return 521;
    prepare(&services, &image);
    services.FreePages = 0;
    if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_UNSUPPORTED || calls)
        return 522;
    for (alias_mode = 1; alias_mode <= 5; ++alias_mode) {
        int scenario = alias_mode;
        prepare(&services, &image);
        alias_mode = scenario;
        if (scenario == 2) { fixture[104] = 0; fixture[105] = 16; }
        fail_release = 1;
        if (elf_stage_segments(&services, fixture, sizeof(fixture), &image) != EFI_DEVICE_ERROR ||
            calls != 2 || released != 1 || image.Count != 1 || bad_mock ||
            image.AllocatedBytes != (scenario == 2 ? 8192ULL : 4096ULL)) return 530 + scenario;
        for (byte = 0; byte < sizeof(memory[1]); ++byte)
            if (memory[1][byte] != 0xcc) return 540 + scenario;
        for (byte = 0; byte < 16; ++byte)
            if (memory[0][256 + byte] != (UINT8)(byte + 1)) return 550 + scenario;
        if (fixture[0] != 0x7f || fixture[384] != 33) return 560 + scenario;
        fail_release = 0;
        if (elf_release_segments(&services, &image) != EFI_SUCCESS || released != 2 ||
            image.Count || image.AllocatedBytes || bad_mock) return 570 + scenario;
    }
    return 0;
}



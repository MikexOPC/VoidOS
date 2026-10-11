#include "memory/map.h"
#include "memory/regions.h"

static UINT8 buffer[4096];
static UINTN allocations, frees, queries;
static int scenario, owned, mock_error;

// ====== servizi simulati con controllo della proprieta ====== //

static EFI_STATUS EFIAPI allocate(UINT32 type, UINTN size, void **result)
{
    ++allocations;
    if (owned || type != 2 || size > sizeof(buffer)) {
        mock_error = 1;
        return EFI_OUT_OF_RESOURCES;
    }
    *result = buffer;
    if (scenario == 1)
        return EFI_OUT_OF_RESOURCES;
    owned = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(void *allocation)
{
    ++frees;
    if (!owned || allocation != buffer) {
        mock_error = 1;
        return EFI_INVALID_PARAMETER;
    }
    if (scenario == 2 || scenario == 15)
        return EFI_DEVICE_ERROR;
    owned = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI get_map(UINTN *size, void *allocation, UINTN *key,
                               UINTN *stride, UINT32 *version)
{
    UINTN index;
    UINT8 *bytes = (UINT8 *)allocation;
    ++queries;
    *stride = scenario == 4 ? 24 : 48;
    *version = scenario == 5 ? 2 : 1;
    if (!allocation) {
        *size = scenario == 3 ? ~0ULL : 96;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (!owned || *size > sizeof(buffer)) {
        mock_error = 1;
        return EFI_INVALID_PARAMETER;
    }
    if ((scenario == 6 || scenario == 15) && queries == 2) {
        *size += 48;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (scenario == 7) {
        *size += 48;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (scenario == 8)
        return EFI_DEVICE_ERROR;
    if (scenario == 9) {
        ++*size;
        return EFI_SUCCESS;
    }
    for (index = 0; index < 96; ++index)
        bytes[index] = 0;
    bytes[24] = bytes[72] = 1;
    bytes[57] = 16;
    if (scenario == 10)
        bytes[57] = 0;
    if (scenario == 11)
        bytes[8] = 1;
    if (scenario == 12)
        for (index = 24; index < 32; ++index)
            bytes[index] = 255;
    if (scenario == 13)
        bytes[24] = 0;
    *size = scenario == 14 ? 95 : 96;
    *key = 123;
    return EFI_SUCCESS;
}

// ====== errori, crescita e rilascio ritentabile ====== //

__declspec(dllexport) int run_map_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services;
    BOOT_MEMORY_MAP map = {0};
    BOOT_MEMORY_REGION region;
    EFI_STATUS status, expected;
    int test;
    if ((UINTN)((UINT8 *)&services.GetMemoryMap - (UINT8 *)&services) != 56)
        return 701;
    services.Hdr.HeaderSize = sizeof(services);
    services.GetMemoryMap = get_map;
    services.AllocatePool = allocate;
    services.FreePool = release;
    for (test = 0; test <= 15; ++test) {
        scenario = test;
        owned = mock_error = 0;
        allocations = frees = queries = 0;
        expected = EFI_LOAD_ERROR;
        if (test == 0 || test == 2 || test == 6) expected = EFI_SUCCESS;
        if (test == 1 || test == 3 || test == 7) expected = EFI_OUT_OF_RESOURCES;
        if (test == 8 || test == 15) expected = EFI_DEVICE_ERROR;
        status = boot_acquire_memory_map(&services, &map);
        if (status != expected || mock_error || (status != EFI_SUCCESS && map.Valid))
            return 710 + test;
        if (status == EFI_SUCCESS && (!map.Valid || map.Size != 96 || map.Key != 123 ||
            map.DescriptorSize != 48 || map.DescriptorVersion != 1))
            return 730;
        if (status == EFI_SUCCESS && (boot_read_memory_region(&map, 0, &region) != EFI_SUCCESS ||
            region.Pages != 1 || region.FirmwareType || region.ConventionalCandidate))
            return 738;
        if (test == 6 && (allocations != 2 || frees != 1))
            return 731;
        if (test == 7 && (allocations != 4 || queries != 5))
            return 732;
        if (map.Buffer && boot_acquire_memory_map(&services, &map) != EFI_INVALID_PARAMETER)
            return 733;
        status = boot_release_memory_map(&services, &map);
        if (boot_read_memory_region(&map, 0, &region) != EFI_INVALID_PARAMETER || region.Pages)
            return 739;
        if (test == 2 || test == 15) {
            if (status != EFI_DEVICE_ERROR || !map.Buffer || map.Valid || map.Key || !owned)
                return 734;
            scenario = 0;
            status = boot_release_memory_map(&services, &map);
        }
        if (status != EFI_SUCCESS || map.Buffer || map.Capacity || map.Valid || owned || mock_error)
            return 735;
        if (boot_release_memory_map(&services, &map) != EFI_SUCCESS || mock_error)
            return 736;
    }
    services.GetMemoryMap = 0;
    if (boot_acquire_memory_map(&services, &map) != EFI_UNSUPPORTED ||
        boot_acquire_memory_map(&services, 0) != EFI_INVALID_PARAMETER)
        return 737;
    return 0;
}

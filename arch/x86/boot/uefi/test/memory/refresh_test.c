#include "memory/final/refresh.h"

static UINT8 buffer[256];
static UINT32 scenario, queries;

// ====== simulazione GetMemoryMap senza allocatore o FreePool ====== //

static EFI_STATUS EFIAPI get_map(UINTN *size, void *destination, UINTN *key,
                               UINTN *stride, UINT32 *version)
{
    UINTN byte;
    ++queries;
    if (destination != buffer || *size != sizeof(buffer)) return EFI_INVALID_PARAMETER;
    *key = 100 + queries;
    *stride = scenario == 3 ? 39 : 48;
    *version = scenario == 4 ? 2 : 1;
    if (scenario == 1) { *size = sizeof(buffer) + 48; return EFI_BUFFER_TOO_SMALL; }
    if (scenario == 2) return EFI_DEVICE_ERROR;
    for (byte = 0; byte < 96; ++byte) buffer[byte] = 0;
    buffer[0] = buffer[48] = 2;
    buffer[24] = buffer[72] = 1;
    buffer[57] = scenario == 5 ? 0 : 16;
    *size = scenario == 6 ? sizeof(buffer) + 1 : scenario == 7 ? 95 : 96;
    return EFI_SUCCESS;
}

// ====== fotografie aggiornate errori e conservazione del buffer ====== //

__declspec(dllexport) int run_map_refresh_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_MEMORY_MAP map = {0};
    UINT32 test;
    EFI_STATUS expected;
    services.Hdr.HeaderSize = sizeof(services);
    services.GetMemoryMap = get_map;
    map.Buffer = buffer;
    map.Capacity = sizeof(buffer);
    queries = 0;
    for (test = 0; test < 8; ++test) {
        scenario = test;
        map.Valid = 1;
        map.Size = 96;
        map.Key = 777;
        map.DescriptorSize = 48;
        map.DescriptorVersion = 1;
        expected = test == 0 ? EFI_SUCCESS : test == 1 ? EFI_BUFFER_TOO_SMALL :
            test == 2 ? EFI_DEVICE_ERROR : EFI_LOAD_ERROR;
        if (boot_refresh_memory_map(&services, &map) != expected ||
            map.Buffer != buffer || map.Capacity != sizeof(buffer) || queries != test + 1)
            return 800 + (int)test;
        if (!test) {
            if (map.Valid != 1 || map.Key != 101 || map.Size != 96) return 810;
        } else if (map.Valid || map.Size || map.Key || map.DescriptorSize || map.DescriptorVersion)
            return 820 + (int)test;
    }
    scenario = 0;
    if (boot_refresh_memory_map(&services, &map) != EFI_SUCCESS || map.Key != 109) return 830;
    services.GetMemoryMap = 0;
    if (boot_refresh_memory_map(&services, &map) != EFI_UNSUPPORTED || map.Valid || queries != 9)
        return 831;
    map.Capacity = BOOT_MAP_MAX_BYTES + 1;
    if (boot_refresh_memory_map(&services, &map) != EFI_INVALID_PARAMETER || map.Buffer != buffer)
        return 832;
    if (boot_refresh_memory_map(&services, 0) != EFI_INVALID_PARAMETER) return 833;
    boot_forget_map_snapshot(0);
    return 0;
}

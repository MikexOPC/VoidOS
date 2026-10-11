#include "memory/regions.h"
#include "memory/final/refresh.h"

static UINT8 descriptors[(BOOT_MAP_MAX_DESCRIPTORS + 1) * 48];
static UINT64 bases[24];
static UINTN spans[24];
static UINT32 mode, queries, allocations, releases;
static BOOT_MEMORY_MAP *target;
static EFI_BOOT_SERVICES_PREFIX *service_target;
static UINT64 returned_address;

// ====== descrittori numerici e oracolo indipendente a piccoli intervalli ====== //

static void descriptor(UINTN index, UINT64 base, UINTN pages)
{
    UINTN byte;
    UINT8 *output = descriptors + index * 48;
    for (byte = 0; byte < 48; ++byte) output[byte] = 0;
    output[0] = 2;
    for (byte = 0; byte < 8; ++byte) {
        output[8 + byte] = (UINT8)(base >> (byte * 8));
        output[24 + byte] = (UINT8)((UINT64)pages >> (byte * 8));
    }
}

static int reference_overlap(UINTN count)
{
    UINTN first, second, page;
    for (first = 0; first < count; ++first)
        for (second = 0; second < first; ++second)
            for (page = 0; page < spans[first]; ++page) {
                UINT64 address = bases[first] + page * 4096;
                if (address >= bases[second] && address < bases[second] + spans[second] * 4096) return 1;
            }
    return 0;
}

static BOOT_MEMORY_MAP make_map(UINTN count)
{
    BOOT_MEMORY_MAP map = {0};
    map.Buffer = descriptors;
    map.Capacity = sizeof(descriptors);
    map.Size = count * 48;
    map.DescriptorSize = 48;
    map.DescriptorVersion = map.Valid = 1;
    return map;
}

// ====== mappe ordinate disordinate e sovrapposizioni non adiacenti ====== //

static int check_map_orders(void)
{
    BOOT_MEMORY_MAP map = make_map(BOOT_MAP_MAX_DESCRIPTORS);
    UINTN index, test;
    UINT32 seed = 1234567;
    for (index = 0; index < BOOT_MAP_MAX_DESCRIPTORS; ++index) descriptor(index, index * 8192, 1);
    if (boot_validate_memory_map(&map) != EFI_SUCCESS) return 1400;
    descriptor(BOOT_MAP_MAX_DESCRIPTORS - 1, (BOOT_MAP_MAX_DESCRIPTORS - 2) * 8192, 1);
    if (boot_validate_memory_map(&map) != EFI_LOAD_ERROR) return 1401;
    map.Size += 48;
    if (boot_validate_memory_map(&map) != EFI_OUT_OF_RESOURCES) return 1402;
    map = make_map(4);
    descriptor(0, 0x10000, 2);
    descriptor(1, 0x30000, 1);
    descriptor(2, 0x20000, 1);
    descriptor(3, 0x11000, 1);
    if (boot_validate_memory_map(&map) != EFI_LOAD_ERROR) return 1403;
    descriptor(3, 0x40000, 1);
    if (boot_validate_memory_map(&map) != EFI_SUCCESS) return 1404;
    for (test = 0; test < 512; ++test) {
        map = make_map(24);
        for (index = 0; index < 24; ++index) {
            seed = seed * 1664525U + 1013904223U;
            bases[index] = (UINT64)((seed >> 8) & 127) * 4096;
            spans[index] = (seed & 3) + 1;
            if (!(test & 1)) { bases[index] = index * 8192; spans[index] = 1; }
        }
        if ((test & 3) == 2) {
            for (index = 23; index > 0; --index) {
                UINTN other;
                UINT64 saved = bases[index];
                seed = seed * 1664525U + 1013904223U;
                other = seed % (index + 1);
                bases[index] = bases[other];
                bases[other] = saved;
            }
        }
        for (index = 0; index < 24; ++index) {
            descriptor(index, bases[index], spans[index]);
        }
        if (boot_validate_memory_map(&map) != (reference_overlap(24) ? EFI_LOAD_ERROR : EFI_SUCCESS))
            return 1405;
    }
    return 0;
}

// ====== buffer numericamente invalidi rifiutati senza dereferenziarli ====== //

static EFI_STATUS EFIAPI query(UINTN *size, void *buffer, UINTN *key, UINTN *stride, UINT32 *version)
{
    (void)key;
    ++queries;
    *stride = 48;
    *version = 1;
    *size = 96;
    return buffer ? EFI_DEVICE_ERROR : EFI_BUFFER_TOO_SMALL;
}

static EFI_STATUS EFIAPI allocate(UINT32 type, UINTN size, void **buffer)
{
    if (type != 2 || size != 480) return EFI_DEVICE_ERROR;
    ++allocations;
    returned_address = mode == 0 ? ~0ULL - 127 : mode == 1 ? (UINT64)(UINTN)target :
                       (UINT64)(UINTN)service_target + (mode == 3 ? sizeof(*service_target) : 0);
    *buffer = (void *)(UINTN)returned_address;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(void *buffer)
{
    if ((UINT64)(UINTN)buffer != returned_address) return EFI_DEVICE_ERROR;
    ++releases;
    return EFI_SUCCESS;
}

static int check_buffer_guards(void)
{
    BOOT_MEMORY_MAP map = make_map(1);
    BOOT_MEMORY_REGION region;
    static EFI_BOOT_SERVICES_PREFIX services;
    services.Hdr.HeaderSize = sizeof(services) + 80;
    services.GetMemoryMap = query;
    services.AllocatePool = allocate;
    services.FreePool = release;
    map.Buffer = (void *)(UINTN)(~0ULL - 127);
    map.Capacity = 480;
    queries = 0;
    if (boot_validate_memory_map(&map) != EFI_LOAD_ERROR ||
        boot_read_memory_region(&map, 0, &region) != EFI_INVALID_PARAMETER || region.Pages ||
        boot_refresh_memory_map(&services, &map) != EFI_INVALID_PARAMETER || queries || map.Valid)
        return 1410;
    for (mode = 0; mode < 4; ++mode) {
        map = (BOOT_MEMORY_MAP){0};
        target = &map;
        service_target = &services;
        queries = allocations = releases = 0;
        if (boot_acquire_memory_map(&services, &map) != EFI_DEVICE_ERROR || queries != 1 ||
            allocations != 1 || releases || map.Valid) return 1411 + (int)mode;
        if (!mode) {
            if ((UINT64)(UINTN)map.Buffer != returned_address || map.Capacity != 480 ||
                boot_release_memory_map(&services, &map) != EFI_SUCCESS || releases != 1) return 1414;
        } else if (map.Buffer || map.Capacity) return 1415;
    }
    map = make_map(1);
    map.Buffer = &map;
    queries = 0;
    if (boot_refresh_memory_map(&services, &map) != EFI_INVALID_PARAMETER || queries) return 1416;
    map.Buffer = &services;
    map.Capacity = sizeof(services);
    if (boot_refresh_memory_map(&services, &map) != EFI_INVALID_PARAMETER || queries ||
        services.GetMemoryMap != query || services.AllocatePool != allocate) return 1417;
    return 0;
}

// ====== ingresso host della revisione di memoria ====== //

__declspec(dllexport) int run_map_validation_tests(void)
{
    int result = check_map_orders();
    return result ? result : check_buffer_guards();
}

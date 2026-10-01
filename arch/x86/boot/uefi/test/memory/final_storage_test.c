#include "memory/final/storage.h"

__declspec(align(4096)) static UINT8 storage[4 * 4096];
static UINT32 scenario, queries, allocations, releases, release_error, mock_error;
static UINT64 last_address;
static UINTN last_pages;
static BOOT_FINAL_MEMORY_MAP *current_owner;

// ====== firmware simulato e descrittore delle pagine possedute ====== //

static void write64(UINT8 *bytes, UINT64 value)
{
    UINTN index;
    for (index = 0; index < 8; ++index) bytes[index] = (UINT8)(value >> (index * 8));
}

static EFI_STATUS EFIAPI query_map(UINTN *size, void *buffer, UINTN *key, UINTN *stride, UINT32 *version)
{
    UINTN index;
    ++queries;
    *stride = scenario == 4 ? 39 : scenario == 25 ? 4097 : 48;
    *version = 1;
    if (!buffer) {
        if (*size) mock_error = 1;
        *size = scenario == 3 ? 0 : scenario == 5 ? BOOT_MAP_MAX_BYTES :
                scenario == 26 ? BOOT_MAP_MAX_BYTES - 383 :
                scenario == 21 ? BOOT_MAP_MAX_BYTES - 384 :
                (scenario == 13 || scenario == 19) ? 4096 : 96;
        return scenario == 1 ? EFI_DEVICE_ERROR : scenario == 2 ? EFI_SUCCESS : EFI_BUFFER_TOO_SMALL;
    }
    if (buffer != storage || *size != last_pages * 4096) {
        mock_error = 1;
        return EFI_DEVICE_ERROR;
    }
    if (scenario == 14) { *size += 48; return EFI_BUFFER_TOO_SMALL; }
    if (scenario == 15) return EFI_DEVICE_ERROR;
    for (index = 0; index < 48; ++index) ((UINT8 *)buffer)[index] = 0;
    ((UINT8 *)buffer)[0] = scenario == 17 ? 7 : 2;
    write64((UINT8 *)buffer + 8, last_address + (scenario == 20 ? 4096 : 0));
    write64((UINT8 *)buffer + 24, scenario == 19 ? 1 : last_pages);
    write64((UINT8 *)buffer + 32, scenario == 18 ? 1ULL << 63 : 0);
    *size = 48;
    *key = scenario == 22 ? 0 : queries * 10;
    *version = scenario == 16 ? 2 : 1;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pages(UINT32 type, UINT32 memory, UINTN count, UINT64 *address)
{
    ++allocations;
    if (type || memory != 2 || !count || count > BOOT_MAP_MAX_BYTES / 4096) {
        mock_error = 1;
        return EFI_INVALID_PARAMETER;
    }
    if (scenario == 6) return EFI_OUT_OF_RESOURCES;
    last_pages = count;
    last_address = scenario == 7 ? 0 : scenario == 8 ? (UINT64)(UINTN)storage + 1 :
                   scenario == 9 || scenario == 21 ? 1ULL << 52 :
                   scenario == 10 ? 0x400000 : scenario == 11 ? 0xc00000 :
                   scenario == 12 ? (UINT64)(UINTN)current_owner :
                   scenario == 23 ? 0x400000 - 4095 : scenario == 24 ? 0x800000 : (UINT64)(UINTN)storage;
    *address = last_address;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI free_pages(UINT64 address, UINTN count)
{
    ++releases;
    if (address != last_address || count != last_pages) mock_error = 1;
    return release_error ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

// ====== risorse numeriche e configurazione della singola prova ====== //

static void prepare(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MAPPING_PLAN *plan, BOOT_PAGE_TABLES *tables)
{
    UINTN byte;
    *services = (EFI_BOOT_SERVICES_PREFIX){0};
    *plan = (BOOT_MAPPING_PLAN){0};
    *tables = (BOOT_PAGE_TABLES){0};
    services->Hdr.HeaderSize = sizeof(*services);
    services->GetMemoryMap = query_map;
    services->AllocatePages = allocate_pages;
    services->FreePages = free_pages;
    plan->Entry = 0x100100;
    plan->Count = 2;
    plan->StackTop = BOOT_STACK_VIRTUAL + 65536;
    plan->Mappings[0].PhysicalBase = 0x400000;
    plan->Mappings[0].VirtualBase = 0x100000;
    plan->Mappings[0].Pages = 1;
    plan->Mappings[0].Flags = 5;
    plan->Mappings[1].PhysicalBase = 0x800000;
    plan->Mappings[1].VirtualBase = BOOT_STACK_VIRTUAL;
    plan->Mappings[1].Pages = 16;
    plan->Mappings[1].Flags = 6;
    tables->Root = tables->Pages[0] = 0xc00000;
    tables->Count = tables->Valid = 1;
    queries = allocations = releases = release_error = mock_error = 0;
    last_address = last_pages = 0;
    for (byte = 0; byte < sizeof(storage); ++byte) storage[byte] = 0xa5;
}

// ====== errori limiti e assenza di accessi alle allocazioni ambigue ====== //

static int check_scenarios(void)
{
    UINT32 test;
    for (test = 0; test < 27; ++test) {
        EFI_BOOT_SERVICES_PREFIX services;
        BOOT_MAPPING_PLAN plan;
        BOOT_PAGE_TABLES tables;
        BOOT_FINAL_MEMORY_MAP owner = {0};
        EFI_STATUS expected, result;
        UINTN byte;
        scenario = test;
        current_owner = &owner;
        prepare(&services, &plan, &tables);
        expected = test == 1 ? EFI_DEVICE_ERROR :
                   (test >= 2 && test <= 4) || test == 25 ? EFI_LOAD_ERROR :
                   test == 5 || test == 6 || test == 26 ? EFI_OUT_OF_RESOURCES :
                   (test >= 7 && test <= 12) || test == 21 || test == 23 || test == 24 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
        result = boot_allocate_final_map(&services, &plan, &tables, 52, &owner);
        if (result != expected || mock_error) return 1100 + (int)test;
        if (result == EFI_SUCCESS) {
            if (owner.Snapshot.Buffer != storage || owner.Snapshot.Capacity != last_pages * 4096 ||
                owner.Pages != last_pages || owner.Snapshot.Valid) return 1130;
            for (byte = 0; byte < owner.Snapshot.Capacity; ++byte) if (storage[byte]) return 1131;
            for (; byte < sizeof(storage); ++byte) if (storage[byte] != 0xa5) return 1132;
            if (boot_allocate_final_map(&services, &plan, &tables, 52, &owner) != EFI_INVALID_PARAMETER || allocations != 1)
                return 1133;
            expected = test == 14 ? EFI_BUFFER_TOO_SMALL : test == 15 ? EFI_DEVICE_ERROR :
                       test >= 16 && test <= 20 ? EFI_LOAD_ERROR : EFI_SUCCESS;
            if (boot_refresh_final_map(&services, &owner) != expected || allocations != 1 || releases || mock_error)
                return 1140 + (int)test;
            if (expected == EFI_SUCCESS) {
                if (owner.Snapshot.Valid != 1 || owner.Snapshot.Size != 48 ||
                    owner.Snapshot.Key != (test == 22 ? 0 : 20)) return 1170;
            } else if (owner.Snapshot.Valid || owner.Snapshot.Size || owner.Snapshot.Key || owner.Snapshot.DescriptorSize)
                return 1171;
            if (test == 13 && owner.Pages != 2) return 1172;
        } else {
            for (byte = 0; byte < sizeof(storage); ++byte) if (storage[byte] != 0xa5) return 1173;
            if (owner.Snapshot.Valid) return 1174;
        }
        if (owner.Pages) {
            release_error = 1;
            if (boot_release_final_map(&services, &owner) != EFI_DEVICE_ERROR || owner.Pages != last_pages ||
                (UINT64)(UINTN)owner.Snapshot.Buffer != last_address || owner.Snapshot.Valid || owner.Snapshot.Size)
                return 1180;
            release_error = 0;
            if (boot_release_final_map(&services, &owner) != EFI_SUCCESS || owner.Pages ||
                owner.Snapshot.Buffer || owner.Snapshot.Capacity || releases != 2 || mock_error) return 1181;
        } else if (releases) return 1182;
        byte = releases;
        if (boot_release_final_map(&services, &owner) != EFI_SUCCESS || releases != byte) return 1183;
        if (test == 21 && last_pages != BOOT_MAP_MAX_BYTES / 4096) return 1184;
    }
    return 0;
}

// ====== metadati invalidi rifiutati prima di interrogare il firmware ====== //

static int check_arguments(void)
{
    EFI_BOOT_SERVICES_PREFIX services;
    BOOT_MAPPING_PLAN plan;
    BOOT_PAGE_TABLES tables;
    BOOT_FINAL_MEMORY_MAP owner = {0};
    UINT32 test;
    scenario = 0;
    prepare(&services, &plan, &tables);
    current_owner = &owner;
    for (test = 0; test < 8; ++test) {
        BOOT_PAGE_TABLES bad = tables;
        switch (test) {
        case 0: bad.Valid = 0; break;
        case 1: bad.Count = 0; break;
        case 2: bad.Count = BOOT_TABLE_LIMIT + 1; break;
        case 3: bad.Root++; break;
        case 4: bad.Root = bad.Pages[0] = 0x400000; break;
        case 5: bad.Root = bad.Pages[0] = 1ULL << 52; break;
        case 6: bad.Root = bad.Pages[0] = 0; break;
        default: bad.Pages[1] = bad.Pages[0]; bad.Count = 2; break;
        }
        if (boot_allocate_final_map(&services, &plan, &bad, 52, &owner) != EFI_INVALID_PARAMETER || queries || allocations)
            return 1190 + (int)test;
    }
    if (boot_allocate_final_map(&services, &plan, &tables, 31, &owner) != EFI_INVALID_PARAMETER ||
        boot_allocate_final_map(&services, &plan, &tables, 53, &owner) != EFI_INVALID_PARAMETER ||
        boot_allocate_final_map(0, &plan, &tables, 52, &owner) != EFI_UNSUPPORTED ||
        boot_allocate_final_map(&services, &plan, &tables, 52, 0) != EFI_INVALID_PARAMETER || queries || allocations)
        return 1200;
    if (boot_refresh_final_map(&services, &owner) != EFI_INVALID_PARAMETER ||
        boot_refresh_final_map(&services, 0) != EFI_INVALID_PARAMETER ||
        boot_release_final_map(&services, 0) != EFI_INVALID_PARAMETER || queries || releases) return 1201;
    return 0;
}

// ====== ingresso dei test del proprietario persistente ====== //

__declspec(dllexport) int run_final_storage_tests(void)
{
    int result = check_scenarios();
    return result ? result : check_arguments();
}

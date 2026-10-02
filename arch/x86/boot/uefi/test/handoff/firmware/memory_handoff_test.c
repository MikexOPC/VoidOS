#include "memory/final/mapping.h"
#include "paging/verification/contents.h"
#include "handoff/firmware/terminal/handoff.h"

__declspec(align(4096)) static UINT64 arena[32][512];
__declspec(align(4096)) static UINT8 contract[4096];
__declspec(align(4096)) static UINT8 storage[8192];
__declspec(align(4096)) static BOOT_MAPPING_PLAN isolated_plan;
__declspec(align(4096)) static BOOT_MAPPING isolated_mapping;
static BOOT_PAGE_TABLES tables;
static UINT32 allocated, released, fail_at, scenario, queries;
static UINT32 terminal_mode, terminal_case, exits, forbidden, closed;
static int image_token;
static BOOT_FINAL_MEMORY_MAP *terminal_memory;

// ====== pagine vive e rifiuto di allocazioni ambigue ====== //

static EFI_STATUS EFIAPI allocate(UINT32 mode, UINT32 type, UINTN count, UINT64 *address)
{
    if (mode || type != 2 || count != 1 || allocated == fail_at || allocated >= 32)
        return EFI_OUT_OF_RESOURCES;
    *address = (UINT64)(UINTN)arena[allocated];
    if (scenario == 8 && queries) *address = (UINT64)(UINTN)arena[0];
    else if (scenario == 9 && queries) *address = (UINT64)(UINTN)&isolated_plan;
    else if (scenario == 10 && queries) *address = (UINT64)(UINTN)&isolated_mapping;
    else ++allocated;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(UINT64 address, UINTN count)
{
    UINT32 index;
    if (count != 1) return EFI_INVALID_PARAMETER;
    for (index = 0; index < allocated; ++index) {
        if (address == (UINT64)(UINTN)arena[index]) { ++released; return EFI_SUCCESS; }
    }
    return EFI_INVALID_PARAMETER;
}

// ====== descrittori con passo indipendente dalla struttura C ====== //

static void put64(UINT8 *destination, UINT64 value)
{
    UINT32 byte;
    for (byte = 0; byte < 8; ++byte) destination[byte] = (UINT8)(value >> (byte * 8));
}

static EFI_STATUS EFIAPI get_map(UINTN *bytes, void *buffer, UINTN *key, UINTN *stride, UINT32 *version)
{
    UINT32 index;
    UINTN step, count, needed;
    ++queries;
    if (terminal_mode && closed) { ++forbidden; return EFI_DEVICE_ERROR; }
    if (terminal_mode && terminal_case == 5) return EFI_DEVICE_ERROR;
    if (terminal_mode && terminal_case == 6 && queries == 2) { *bytes = 8193; return EFI_BUFFER_TOO_SMALL; }
    step = terminal_mode && terminal_case == 1 && queries == 2 ? 64 : 48;
    count = (UINTN)tables.Count + 4 + (terminal_mode ? queries - 1 : 0);
    needed = count * step;
    if (queries > 1 && scenario == 4) return EFI_DEVICE_ERROR;
    if (queries > 1 && scenario == 5) { *bytes = 8193; return EFI_BUFFER_TOO_SMALL; }
    if (buffer != storage || *bytes != sizeof(storage)) return EFI_INVALID_PARAMETER;
    for (index = 0; index < needed; ++index) storage[index] = 0;
    for (index = 0; index < count; ++index) {
        UINT64 address = index == 0 ? 0x400000 : index == 1 ? 0x800000 :
            index == 2 ? (UINT64)(UINTN)contract : index == 3 ? (UINT64)(UINTN)storage :
            index < tables.Count + 4 ? tables.Pages[index - 4] : 0x1200000ULL + (index - tables.Count - 4) * 0x200000ULL;
        storage[index * step] = (scenario == 6 && queries > 1 && index == tables.Count + 3) ||
            (terminal_mode && index == 0 && (terminal_case == 8 || (terminal_case == 7 && queries == 2))) ? 7 : 2;
        put64(storage + index * step + 8, address);
        put64(storage + index * step + 24, index == 1 ? 16 : index == 3 ? 2 : 1);
    }
    *bytes = needed;
    *key = terminal_mode ? (terminal_case == 14 ? 0 : queries * 100) : queries == 1 ? 10 : 20;
    *stride = step;
    *version = terminal_mode && terminal_case == 15 && queries == 2 ? 2 : 1;
    if (terminal_mode && terminal_case == 9) arena[0][0] |= 4;
    return EFI_SUCCESS;
}

// ====== uscita simulata che controlla il contratto prima di ogni tentativo ====== //

static EFI_STATUS EFIAPI exit_services(EFI_HANDLE image, UINTN key)
{
    const BOOT_START_INFO *info = (const BOOT_START_INFO *)contract;
    const BOOT_MEMORY_MAP *map = &terminal_memory->Snapshot;
    if (closed || image != &image_token || key != map->Key || key != (terminal_case == 14 ? 0 : queries * 100) ||
        info->Signature != BOOT_START_SIGNATURE || info->EnabledFeatures || info->MemoryMapBytes != map->Size ||
        info->MemoryDescriptorBytes != map->DescriptorSize || info->MemoryDescriptorVersion != map->DescriptorVersion) {
        ++forbidden;
        return EFI_DEVICE_ERROR;
    }
    ++exits;
    if (terminal_case == 3 || (terminal_case == 1 && exits == 1) || (terminal_case == 2 && exits < 3) ||
        ((terminal_case == 6 || terminal_case == 7 || terminal_case == 15) && exits == 1)) return EFI_INVALID_PARAMETER;
    if (terminal_case == 4) return EFI_DEVICE_ERROR;
    closed = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI deny_pages(UINT32 mode, UINT32 type, UINTN count, UINT64 *address)
{
    (void)mode; (void)type; (void)count; (void)address;
    ++forbidden;
    return EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI deny_free_pages(UINT64 address, UINTN count)
{
    (void)address; (void)count;
    ++forbidden;
    return EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI deny_pool(UINT32 type, UINTN bytes, void **buffer)
{
    (void)type; (void)bytes; (void)buffer;
    ++forbidden;
    return EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI deny_free_pool(void *buffer)
{
    (void)buffer;
    ++forbidden;
    return EFI_DEVICE_ERROR;
}

// ====== tentativi terminali senza allocazioni con stride e dimensione variabili ====== //

static int test_terminal(const EFI_BOOT_SERVICES_PREFIX *base, const BOOT_KERNEL_IMAGE *kernel,
                         const BOOT_KERNEL_STACK *stack, const BOOT_PAGING_CAPABILITIES *caps,
                         BOOT_START_PAGE *page, BOOT_FINAL_MEMORY_MAP *owner)
{
    EFI_BOOT_SERVICES_EXIT_PREFIX services = {0};
    BOOT_EXIT_HANDOFF handoff = {kernel, stack, caps, &tables, page, owner};
    BOOT_START_INFO saved = *(BOOT_START_INFO *)contract;
    BOOT_MEMORY_MAP snapshot = owner->Snapshot;
    UINT64 root_entry = arena[0][0];
    UINT32 test;
    terminal_memory = owner;
    services.Prefix = *base;
    services.Prefix.Hdr.HeaderSize = sizeof(services);
    services.Prefix.AllocatePages = deny_pages;
    services.Prefix.FreePages = deny_free_pages;
    services.Prefix.AllocatePool = deny_pool;
    services.Prefix.FreePool = deny_free_pool;
    services.ExitBootServices = exit_services;
    for (test = 0; test < 16; ++test) {
        BOOT_EXIT_STATE state = {0};
        EFI_STATUS expected = test <= 2 || test == 14 ? EFI_SUCCESS :
            test == 3 || test == 11 || test == 12 || test == 13 ? EFI_INVALID_PARAMETER :
            test == 6 ? EFI_BUFFER_TOO_SMALL : test == 7 || test == 8 || test == 15 ? EFI_LOAD_ERROR :
            test == 10 ? EFI_UNSUPPORTED : EFI_DEVICE_ERROR;
        UINT32 expected_attempts = test <= 2 ? test + 1 : test == 3 ? 3 :
            test == 4 || test == 6 || test == 7 || test == 14 || test == 15 ? 1 : 0;
        UINT32 initial_phase = test == 12 ? BOOT_SERVICES_RESTRICTED : BOOT_SERVICES_AVAILABLE;
        UINT32 expected_phase = expected == EFI_SUCCESS ? BOOT_SERVICES_EXITED :
            expected_attempts || test == 12 ? BOOT_SERVICES_RESTRICTED : BOOT_SERVICES_AVAILABLE;
        UINT32 before_allocations = allocated, before_releases = released;
        terminal_mode = scenario = closed = 0;
        queries = 0;
        arena[0][0] = root_entry;
        owner->Snapshot = snapshot;
        owner->Pages = 2;
        if (boot_refresh_final_map(&services.Prefix, owner) != EFI_SUCCESS) return 1500;
        *(BOOT_START_INFO *)contract = saved;
        page->Ready = 1;
        if (test == 11) ((BOOT_START_INFO *)contract)->EnabledFeatures = 3;
        if (test == 13) owner->Pages = 0;
        state.Phase = initial_phase;
        queries = exits = forbidden = 0;
        terminal_case = test;
        terminal_mode = 1;
        services.Prefix.Hdr.HeaderSize = test == 10 ? 232 : sizeof(services);
        if (boot_exit_memory_handoff(&image_token, &services, &handoff, &state) != expected ||
            state.Phase != expected_phase || state.Attempts != expected_attempts || exits != expected_attempts ||
            state.LastStatus != (test == 12 ? EFI_SUCCESS : expected) ||
            forbidden || allocated != before_allocations || released != before_releases)
            return 1510 + (int)test;
        if (expected == EFI_SUCCESS) {
            UINT32 previous_queries = queries;
            if (!closed || page->Ready != 1 || owner->Snapshot.Valid != 1 ||
                ((BOOT_START_INFO *)contract)->MemoryMapBytes != owner->Snapshot.Size ||
                boot_exit_memory_handoff(&image_token, &services, &handoff, &state) != EFI_INVALID_PARAMETER ||
                queries != previous_queries || forbidden) return 1540;
        } else if (test != 11 && test != 12 && test != 13 &&
                   (page->Ready || ((BOOT_START_INFO *)contract)->Signature)) return 1550 + (int)test;
        if (test == 11 || test == 12 || test == 13) { if (queries || exits) return 1560; }
        services.Prefix.Hdr.HeaderSize = sizeof(services);
    }
    // ====== stato di uscita distinto dalle risorse e dai servizi ====== //

    for (test = 0; test < 4; ++test) {
        BOOT_EXIT_STATE *alias;
        UINT8 before[sizeof(BOOT_EXIT_STATE)];
        UINT32 byte;
        terminal_mode = scenario = closed = 0;
        arena[0][0] = root_entry;
        owner->Snapshot = snapshot;
        owner->Pages = 2;
        if (boot_refresh_final_map(&services.Prefix, owner) != EFI_SUCCESS) return 1570;
        *(BOOT_START_INFO *)contract = saved;
        page->Ready = 1;
        alias = (BOOT_EXIT_STATE *)(test == 0 ? contract + 64 :
            test == 1 ? (UINT8 *)&arena[0][400] :
            test == 2 ? storage + 4096 : (UINT8 *)&services.Reserved);
        for (byte = 0; byte < sizeof(before); ++byte) before[byte] = ((UINT8 *)alias)[byte];
        queries = exits = forbidden = 0;
        if (boot_exit_memory_handoff(&image_token, &services, &handoff, alias) != EFI_INVALID_PARAMETER ||
            queries || exits || forbidden || page->Ready != 1 ||
            ((BOOT_START_INFO *)contract)->Signature != BOOT_START_SIGNATURE) return 1571 + (int)test;
        for (byte = 0; byte < sizeof(before); ++byte)
            if (((UINT8 *)alias)[byte] != before[byte]) return 1580 + (int)test;
    }
    terminal_mode = 0;
    arena[0][0] = root_entry;
    owner->Pages = 2;
    owner->Snapshot = snapshot;
    *(BOOT_START_INFO *)contract = saved;
    page->Ready = 1;
    return 0;
}

// ====== lettura indipendente delle foglie e della guardia ====== //

static UINT64 leaf(UINT64 root, UINT64 address)
{
    const UINT32 shifts[4] = {39, 30, 21, 12};
    UINT32 level;
    for (level = 0; level < 4; ++level) {
        UINT64 entry = ((UINT64 *)(UINTN)root)[(address >> shifts[level]) & 511];
        if (!(entry & 1)) return 0;
        if (level == 3) return entry;
        root = entry & 0x000ffffffffff000ULL;
        if (root < (UINT64)(UINTN)arena || root >= (UINT64)(UINTN)arena + sizeof(arena)) return ~0ULL;
    }
    return ~0ULL;
}

// ====== contratto e proprietari dopo successo ed errori di ogni livello ====== //

__declspec(dllexport) int run_memory_handoff_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_KERNEL_IMAGE kernel = {0};
    BOOT_KERNEL_STACK stack = {0x800000, 16, 0x810000};
    BOOT_PAGING_CAPABILITIES caps = {52, 1, 1};
    UINT32 test;
    services.Hdr.HeaderSize = sizeof(services);
    services.AllocatePages = allocate;
    services.FreePages = release;
    services.GetMemoryMap = get_map;
    kernel.Pages.Count = kernel.Description.SegmentCount = 1;
    kernel.Pages.AllocatedBytes = kernel.Description.AllocatedBytes = 4096;
    kernel.Pages.Segments[0].PhysicalBase = kernel.Description.Segments[0].PhysicalBase = 0x400000;
    kernel.Pages.Segments[0].VirtualBase = kernel.Description.Segments[0].VirtualBase = 0x100000;
    kernel.Pages.Segments[0].Pages = 1;
    kernel.Description.Segments[0].Bytes = 4096;
    kernel.Pages.Segments[0].Flags = kernel.Description.Segments[0].Flags = 5;
    kernel.Description.Signature = BOOT_SEGMENTS_SIGNATURE;
    kernel.Description.Version = BOOT_SEGMENTS_VERSION;
    kernel.Description.Size = sizeof(kernel.Description);
    kernel.Description.VirtualEntry = 0x100100;
    for (test = 0; test < 15; ++test) {
        BOOT_MAPPING_PLAN plan;
        BOOT_START_PAGE page = {(UINT64)(UINTN)contract, 1, 0};
        BOOT_FINAL_MEMORY_MAP owner = {0};
        BOOT_START_INFO *info = (BOOT_START_INFO *)contract;
        UINT64 root;
        UINT32 baseline, byte;
        EFI_STATUS expected = test >= 1 && test <= 3 ? EFI_OUT_OF_RESOURCES :
            test == 4 || test == 7 || test >= 8 ? EFI_DEVICE_ERROR :
            test == 5 ? EFI_BUFFER_TOO_SMALL : test == 6 ? EFI_LOAD_ERROR : EFI_SUCCESS;
        for (byte = 0; byte < sizeof(tables); ++byte) ((UINT8 *)&tables)[byte] = 0;
        allocated = released = queries = scenario = 0;
        fail_at = ~0U;
        if (boot_plan_mappings(&kernel, &stack, &plan) != EFI_SUCCESS ||
            boot_map_start_page(&plan, &page) != EFI_SUCCESS ||
            boot_build_page_tables(&services, &plan, 52, 1, &tables) != EFI_SUCCESS ||
            boot_prepare_start_info(&plan, &tables, &caps, info) != EFI_SUCCESS) return 1400;
        page.Ready = 1;
        owner.Pages = 2;
        owner.Snapshot.Buffer = storage;
        owner.Snapshot.Capacity = sizeof(storage);
        if (boot_refresh_final_map(&services, &owner) != EFI_SUCCESS) return 1401;
        baseline = allocated;
        root = tables.Root;
        scenario = test;
        if (test >= 9) {
            EFI_STATUS extension_status;
            isolated_plan = plan;
            isolated_mapping.PhysicalBase = (UINT64)(UINTN)storage;
            isolated_mapping.VirtualBase = BOOT_MAP_VIRTUAL;
            isolated_mapping.Pages = 2;
            isolated_mapping.Flags = 4;
            if (test == 11) isolated_mapping.VirtualBase += 4096;
            if (test == 12) isolated_mapping.Flags = 6;
            if (test == 13) isolated_mapping.PhysicalBase = (UINT64)(UINTN)arena[0];
            if (test == 14) isolated_mapping.Pages = 257;
            extension_status = boot_extend_page_tables(&services, &isolated_plan, &isolated_mapping, 52, &tables);
            if (extension_status != (test <= 10 ? EFI_DEVICE_ERROR : EFI_INVALID_PARAMETER) ||
                tables.Count != baseline || allocated != baseline ||
                (test <= 10 ? tables.Root != 0 || tables.Valid != 0 : tables.Root != root || tables.Valid != 1))
                return 1460 + (int)test;
            for (byte = 0; byte < sizeof(plan); ++byte)
                if (((UINT8 *)&plan)[byte] != ((UINT8 *)&isolated_plan)[byte]) return 1469;
            if (boot_release_page_tables(&services, &tables) != EFI_SUCCESS || released != allocated) return 1470;
            continue;
        }
        if (test >= 1 && test <= 3) fail_at = allocated + test - 1;
        if (test == 7) arena[0][0] = 0x12345003;
        if (boot_prepare_memory_handoff(&services, &kernel, &stack, &caps, &tables, &page, &owner) != expected)
            return 1410 + (int)test;
        if (!test) {
            UINT32 offset;
            if (queries != 2 || owner.Snapshot.Key != 20 || tables.Root != root ||
                tables.Count != baseline + 3 || !page.Ready || info->Signature != BOOT_START_SIGNATURE ||
                info->Version != 2 || info->Size != 112 || info->EnabledFeatures ||
                info->MemoryMapVirtual != 0x0000500000000000ULL || info->MemoryMapCapacity != 8192 ||
                info->MemoryMapBytes != ((UINT64)tables.Count + 4) * 48 ||
                info->MemoryDescriptorBytes != 48 || info->MemoryDescriptorVersion != 1 || info->MemoryReserved)
                return 1420;
            for (offset = 0; offset < 8192; offset += 4096)
                if (leaf(root, info->MemoryMapVirtual + offset) != (((UINT64)(UINTN)storage + offset) | BOOT_PTE_NX | 1))
                    return 1421;
            if (leaf(root, info->MemoryMapVirtual + 8192) || leaf(root, BOOT_STACK_GUARD)) return 1422;
            {
                int terminal_result = test_terminal(&services, &kernel, &stack, &caps, &page, &owner);
                if (terminal_result) return terminal_result;
            }
        } else if (test != 7 && (page.Ready || info->Signature || owner.Snapshot.Valid)) return 1430 + (int)test;
        if (test >= 1 && test <= 3 && (tables.Root || tables.Valid || tables.Count != baseline + test - 1)) return 1440;
        if (test == 7 && (allocated != baseline || queries != 1)) return 1441;
        if (test == 8 && tables.Count != baseline) return 1442;
        if (boot_release_page_tables(&services, &tables) != EFI_SUCCESS || released != allocated ||
            boot_release_page_tables(&services, &tables) != EFI_SUCCESS || released != allocated) return 1450 + (int)test;
    }
    return 0;
}

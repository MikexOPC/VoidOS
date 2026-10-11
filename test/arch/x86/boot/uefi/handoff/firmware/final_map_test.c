#include "handoff/firmware/prepare_map.h"

__declspec(align(4096)) static UINT8 storage[4096];
static UINT32 scenario, allocations, releases, queries;

// ====== fotografia completa o risorsa marcata con tipo non ammesso ====== //

static void write64(UINT8 *bytes, UINT64 value)
{
    UINTN index;
    for (index = 0; index < 8; ++index) bytes[index] = (UINT8)(value >> (index * 8));
}

static EFI_STATUS EFIAPI get_map(UINTN *size, void *buffer, UINTN *key, UINTN *stride, UINT32 *version)
{
    UINTN index;
    const UINT64 addresses[5] = {0x400000, 0x800000, 0xc00000, 0x1000000, (UINT64)(UINTN)storage};
    ++queries;
    *stride = 48;
    *version = 1;
    if (!buffer) { *size = 240; return EFI_BUFFER_TOO_SMALL; }
    if (buffer != storage || *size != sizeof(storage)) return EFI_DEVICE_ERROR;
    for (index = 0; index < 240; ++index) storage[index] = 0;
    for (index = 0; index < 5; ++index) {
        storage[index * 48] = scenario == index + 1 ? 7 : 2;
        write64(storage + index * 48 + 8, addresses[index]);
        write64(storage + index * 48 + 24, index == 1 ? 16 : 1);
    }
    *size = 240;
    *key = 99;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate(UINT32 type, UINT32 memory, UINTN count, UINT64 *address)
{
    if (type || memory != 2 || count != 1) return EFI_DEVICE_ERROR;
    ++allocations;
    *address = (UINT64)(UINTN)storage;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI release(UINT64 address, UINTN count)
{
    if (address != (UINT64)(UINTN)storage || count != 1) return EFI_DEVICE_ERROR;
    ++releases;
    return EFI_SUCCESS;
}

// ====== copertura di kernel stack tabelle contratto e mappa ====== //

__declspec(dllexport) int run_final_map_tests(void)
{
    EFI_BOOT_SERVICES_PREFIX services = {0};
    BOOT_KERNEL_IMAGE kernel = {0};
    BOOT_KERNEL_STACK stack = {0};
    BOOT_PAGE_TABLES tables = {0};
    BOOT_PAGING_CAPABILITIES capabilities = {52, 1, 1};
    BOOT_START_PAGE page = {0};
    UINT32 test;
    services.Hdr.HeaderSize = sizeof(services);
    services.GetMemoryMap = get_map;
    services.AllocatePages = allocate;
    services.FreePages = release;
    kernel.Pages.Count = 1;
    kernel.Pages.AllocatedBytes = 4096;
    kernel.Pages.Segments[0].PhysicalBase = 0x400000;
    kernel.Pages.Segments[0].VirtualBase = 0x100000;
    kernel.Pages.Segments[0].Pages = 1;
    kernel.Pages.Segments[0].Flags = 5;
    kernel.Description.Signature = BOOT_SEGMENTS_SIGNATURE;
    kernel.Description.Version = BOOT_SEGMENTS_VERSION;
    kernel.Description.Size = sizeof(kernel.Description);
    kernel.Description.VirtualEntry = 0x100100;
    kernel.Description.AllocatedBytes = 4096;
    kernel.Description.SegmentCount = 1;
    kernel.Description.Segments[0].PhysicalBase = 0x400000;
    kernel.Description.Segments[0].VirtualBase = 0x100000;
    kernel.Description.Segments[0].Bytes = 4096;
    kernel.Description.Segments[0].Flags = 5;
    stack.PhysicalBase = 0x800000;
    stack.Pages = 16;
    stack.Top = stack.PhysicalBase + 65536;
    tables.Root = tables.Pages[0] = 0xc00000;
    tables.Count = tables.Valid = 1;
    page.PhysicalBase = 0x1000000;
    page.Pages = page.Ready = 1;
    for (test = 0; test < 6; ++test) {
        BOOT_FINAL_MEMORY_MAP owner = {0};
        EFI_STATUS expected = test ? EFI_LOAD_ERROR : EFI_SUCCESS;
        scenario = test;
        allocations = releases = queries = 0;
        if (boot_prepare_final_memory_map(&services, &kernel, &stack, &capabilities, &tables, &page, &owner) != expected ||
            owner.Pages != 1 || allocations != 1 || releases || queries != 2 || page.Ready != 1 || tables.Valid != 1)
            return 1310 + (int)test;
        if (!test) {
            if (owner.Snapshot.Valid != 1 || owner.Snapshot.Key != 99 || owner.Snapshot.Size != 240) return 1320;
        } else if (owner.Snapshot.Valid || owner.Snapshot.Size || owner.Snapshot.Key) return 1321;
        if (boot_release_final_map(&services, &owner) != EFI_SUCCESS || releases != 1 || owner.Pages) return 1322;
    }
    {
        BOOT_FINAL_MEMORY_MAP owner = {0};
        allocations = queries = 0;
        page.Ready = 0;
        if (boot_prepare_final_memory_map(&services, &kernel, &stack, &capabilities, &tables, &page, &owner) != EFI_INVALID_PARAMETER ||
            allocations || queries) return 1330;
        page.Ready = 1;
        capabilities.Valid = 0;
        if (boot_prepare_final_memory_map(&services, &kernel, &stack, &capabilities, &tables, &page, &owner) != EFI_INVALID_PARAMETER ||
            allocations || queries) return 1331;
    }
    return 0;
}

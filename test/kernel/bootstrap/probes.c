#include "probes.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
#include "arch/x86/mm/window/access.h"
static KERNEL_OBJECT_POOL object_pool;

// ====== blocco contiguo azzerato e verifica di ogni pagina dopo riuso ===== //

static void prepare_page_window(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    BOOT_U32 index;
    BOOT_U64 pages = X86_KERNEL_WINDOW_PAGES, bytes = X86_KERNEL_WINDOW_BYTES;
    BOOT_U64 budget = memory->Physical.FreePages;
    volatile unsigned char *data;
    if (!x86_page_window_initialize(&memory->Window, &memory->Physical, memory->Context.Info.PageTableRoot) ||
        !x86_page_window_acquire(&memory->Window, pages)) kernel_panic("PAGE_MAPPING_FAILED");
    data = x86_window_bytes();
    for (index = 0; index < bytes; ++index) if (data[index]) kernel_panic("PAGE_NOT_ZERO");
    for (index = 0; index < pages; ++index) {
        data[index * 4096] = 0x5a;
        data[index * 4096 + 4095] = 0xa5;
    }
    for (index = 0; index < pages; ++index)
        if (data[index * 4096] != 0x5a || data[index * 4096 + 4095] != 0xa5) kernel_panic("PAGE_WRITE_FAILED");
    if (kernel_physical_release(&memory->Physical, &memory->Window.Lease) ||
        !x86_page_window_release(&memory->Window)) kernel_panic("PAGE_LIFETIME_FAILED");
    data = 0;
    if (!x86_page_window_acquire(&memory->Window, pages)) kernel_panic("PAGE_REACQUIRE_FAILED");
    data = x86_window_bytes();
    for (index = 0; index < bytes; ++index) if (data[index]) kernel_panic("PAGE_REUSE_NOT_ZERO");
    if (!x86_page_window_release(&memory->Window)) kernel_panic("PAGE_RELEASE_FAILED");
    data = 0;
    if (memory->Physical.FreePages != budget ||
        !kernel_log(0, "KERNEL_MAPPING_OK") || !kernel_log(0, "KERNEL_WINDOW_16_PAGES_OK")) kernel_panic("PAGE_RELEASE_FAILED");
}

// ====== pool esclusivo nel mapping vivo senza reclamare la memoria del loader ===== //

static void prepare_kernel_objects(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    KERNEL_OBJECT_HANDLE first = {0}, second = {0}, stale, third = {0};
    unsigned char *data;
    BOOT_U32 index;
    BOOT_U64 budget = memory->Physical.FreePages;
    if (!x86_page_window_acquire(&memory->Window, X86_KERNEL_WINDOW_PAGES) ||
        !kernel_object_initialize(&object_pool, (void *)x86_window_bytes(), X86_KERNEL_WINDOW_BYTES)) kernel_panic("OBJECT_POOL_INIT_FAILED");
    if (!kernel_object_allocate(&object_pool, 128, 64, &first) ||
        !kernel_object_allocate(&object_pool, 4097, 4096, &second) ||
        !kernel_object_allocate(&object_pool, 96, 32, &third)) kernel_panic("OBJECT_ALLOCATION_FAILED");
    data = kernel_object_data(&object_pool, &first);
    if (!data) kernel_panic("OBJECT_ACCESS_FAILED");
    for (index = 0; index < first.Bytes; ++index) {
        if (data[index]) kernel_panic("OBJECT_NOT_ZERO");
        data[index] = 0xa5;
    }
    data = 0;
    data = kernel_object_data(&object_pool, &second);
    if (!data || ((BOOT_U64)data & 4095)) kernel_panic("OBJECT_ALIGNMENT_FAILED");
    for (index = 0; index < second.Bytes; ++index) if (data[index]) kernel_panic("OBJECT_NOT_ZERO");
    data[0] = 0x5a; data[4096] = 0xa5;
    data = 0;
    stale = first;
    if (kernel_object_detach(&object_pool) || !kernel_object_release(&object_pool, &first) ||
        kernel_object_data(&object_pool, &stale) || !kernel_object_allocate(&object_pool, 128, 64, &first) ||
        first.Id == stale.Id || first.Offset != stale.Offset ||
        kernel_object_release(&object_pool, &stale)) kernel_panic("OBJECT_LIFETIME_FAILED");
    data = kernel_object_data(&object_pool, &first);
    if (!data || ((BOOT_U64)data & 63)) kernel_panic("OBJECT_ALIGNMENT_FAILED");
    for (index = 0; index < first.Bytes; ++index) if (data[index]) kernel_panic("OBJECT_REUSE_NOT_ZERO");
    data = kernel_object_data(&object_pool, &second);
    if (!data || data[0] != 0x5a || data[4096] != 0xa5) kernel_panic("OBJECT_NEIGHBOR_CORRUPTED");
    data = 0;
    stale = first;
    if (!kernel_object_release(&object_pool, &first) || !kernel_object_release(&object_pool, &second) ||
        !kernel_object_release(&object_pool, &third) || object_pool.Used || object_pool.Count ||
        !kernel_object_detach(&object_pool)) kernel_panic("OBJECT_RELEASE_FAILED");
    if (!kernel_object_initialize(&object_pool, (void *)x86_window_bytes(), X86_KERNEL_WINDOW_BYTES) ||
        !kernel_object_allocate(&object_pool, 128, 64, &first) || first.Id <= stale.Id ||
        kernel_object_data(&object_pool, &stale) || !kernel_object_release(&object_pool, &first) ||
        !kernel_object_detach(&object_pool) || object_pool.Arena ||
        !x86_page_window_release(&memory->Window) || memory->Physical.FreePages != budget) kernel_panic("OBJECT_DETACH_FAILED");
    if (!kernel_log(0, "KERNEL_OBJECTS_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== ciclo ownership fisico senza leggere o scrivere pagine non mappate ===== //

static void prepare_physical_memory(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    KERNEL_PHYSICAL_LEASE first = {0}, second = {0}, stale, reused = {0};
    BOOT_U64 total;
    if (!kernel_physical_initialize(&memory->Physical, memory->Context.MemoryMap, memory->Context.Info.MemoryMapBytes,
                                    memory->Context.Info.MemoryDescriptorBytes, x86_physical_bits())) kernel_panic("PHYSICAL_INIT_FAILED");
    total = memory->Physical.TotalPages;
    if (!kernel_physical_allocate(&memory->Physical, 2, &first) ||
        !kernel_physical_allocate(&memory->Physical, 1, &second) || memory->Physical.FreePages != total - 3)
        kernel_panic("PHYSICAL_ALLOCATION_FAILED");
    stale = first;
    if (!kernel_physical_release(&memory->Physical, &first) || kernel_physical_release(&memory->Physical, &stale) ||
        !kernel_physical_allocate(&memory->Physical, 2, &reused) || kernel_physical_release(&memory->Physical, &stale) ||
        !kernel_physical_release(&memory->Physical, &second) || !kernel_physical_release(&memory->Physical, &reused) ||
        memory->Physical.FreePages != total || !kernel_physical_validate(&memory->Physical)) kernel_panic("PHYSICAL_RELEASE_FAILED");
    if (!kernel_physical_allocate_aligned(&memory->Physical, 4, 16, &first) ||
        !kernel_physical_allocate_aligned(&memory->Physical, 8, 512, &second) ||
        (first.Base & 65535) || (second.Base & 2097151) || memory->Physical.FreePages != total - 12 ||
        kernel_regions_overlap(first.Base, first.Pages, second.Base, second.Pages) ||
        !kernel_physical_validate(&memory->Physical)) kernel_panic("PHYSICAL_ALIGNMENT_FAILED");
    if (!kernel_text("KERNEL_PHYSICAL_64K_BASE: ") || !kernel_number(first.Base) ||
        !kernel_text("\nKERNEL_PHYSICAL_2M_BASE: ") || !kernel_number(second.Base) ||
        !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_physical_release(&memory->Physical, &first) || !kernel_physical_release(&memory->Physical, &second) ||
        memory->Physical.FreePages != total || !kernel_physical_validate(&memory->Physical)) kernel_panic("PHYSICAL_ALIGNED_RELEASE_FAILED");
    if (!kernel_log(0, "KERNEL_PHYSICAL_ALIGNED_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_log(0, "KERNEL_PHYSICAL_OK") || !kernel_text("Pagine fisiche gestite: ") ||
        !kernel_number(total) || !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== blocchi simultanei persistenti con prova di unmap riuso e vicini intatti ===== //

static void prepare_kernel_regions(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    X86_REGION_HANDLE stale, third = {0}, fourth = {0}, extra = {0};
    volatile unsigned char *first, *second;
    BOOT_U32 page, byte;
    BOOT_U64 budget = memory->Physical.FreePages;
    if (!x86_region_initialize(&memory->Regions, &memory->Physical, memory->Context.Info.PageTableRoot) ||
        !x86_region_acquire(&memory->Regions, 32, 16, &memory->Handles[0]) ||
        !x86_region_acquire(&memory->Regions, 16, 16, &memory->Handles[1])) kernel_panic("REGION_INIT_FAILED");
    first = x86_region_data(&memory->Regions, &memory->Handles[0]);
    second = x86_region_data(&memory->Regions, &memory->Handles[1]);
    if (!first || !second) kernel_panic("REGION_ACCESS_FAILED");
    for (page = 0; page < 32; ++page) { first[page * 4096] = 0x5a; first[page * 4096 + 4095] = 0xa5; }
    for (page = 0; page < 16; ++page) { second[page * 4096] = 0xa5; second[page * 4096 + 4095] = 0x5a; }
    first = 0; second = 0; stale = memory->Handles[0];
    if (!x86_region_release(&memory->Regions, &memory->Handles[0]) || x86_region_data(&memory->Regions, &stale) ||
        !x86_region_acquire(&memory->Regions, 32, 16, &memory->Handles[0]) || memory->Handles[0].Id == stale.Id ||
        x86_region_release(&memory->Regions, &stale)) kernel_panic("REGION_REUSE_FAILED");
    first = x86_region_data(&memory->Regions, &memory->Handles[0]);
    second = x86_region_data(&memory->Regions, &memory->Handles[1]);
    if (!first || !second) kernel_panic("REGION_ACCESS_FAILED");
    for (byte = 0; byte < 32 * 4096; ++byte) if (first[byte]) kernel_panic("REGION_REUSE_NOT_ZERO");
    for (page = 0; page < 16; ++page)
        if (second[page * 4096] != 0xa5 || second[page * 4096 + 4095] != 0x5a) kernel_panic("REGION_NEIGHBOR_CORRUPTED");
    first = 0; second = 0;
    if (!x86_region_acquire(&memory->Regions, 1, 1, &third) || !x86_region_acquire(&memory->Regions, 1, 1, &fourth) ||
        x86_region_acquire(&memory->Regions, 1, 1, &extra) || !x86_region_release(&memory->Regions, &third) ||
        !x86_region_release(&memory->Regions, &fourth) || memory->Physical.FreePages != budget - 48 ||
        !x86_region_validate(&memory->Regions)) kernel_panic("REGION_BUDGET_FAILED");
    if (!kernel_text("KERNEL_REGION_PTE: ") || !kernel_number((BOOT_U64)&x86_region_entries(3)[X86_REGION_LEAF]) ||
        !kernel_text("\nKERNEL_REGION0_PHYSICAL: ") || !kernel_number(memory->Regions.Live[0].Base) ||
        !kernel_text("\nKERNEL_REGION1_PHYSICAL: ") || !kernel_number(memory->Regions.Live[1].Base) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_REGIONS_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== contesto e inventario posseduti dal kernel in un mapping persistente ===== //

static void prepare_persistent_objects(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    KERNEL_OBJECT_HANDLE temporary = {0}, stale;
    unsigned char *data;
    BOOT_U32 byte;
    BOOT_U64 budget = memory->Physical.FreePages;
    if (!x86_object_arena_attach(&memory->Arena, &memory->Regions, &memory->Handles[1]) ||
        !x86_object_arena_allocate(&memory->Arena, sizeof(memory->Context), 16, &memory->ContextObject) ||
        !x86_object_arena_allocate(&memory->Arena, sizeof(memory->Inventory), 8, &memory->InventoryObject) ||
        !x86_object_arena_allocate(&memory->Arena, 4097, 4096, &temporary)) kernel_panic("PERSISTENT_OBJECT_INIT_FAILED");
    data = x86_object_arena_data(&memory->Arena, &memory->ContextObject);
    if (!data) kernel_panic("PERSISTENT_OBJECT_ACCESS_FAILED");
    for (byte = 0; byte < sizeof(memory->Context); ++byte)
        ((volatile unsigned char *)data)[byte] = ((const unsigned char *)&memory->Context)[byte];
    data = 0;
    data = x86_object_arena_data(&memory->Arena, &memory->InventoryObject);
    if (!data) kernel_panic("PERSISTENT_OBJECT_ACCESS_FAILED");
    for (byte = 0; byte < sizeof(memory->Inventory); ++byte)
        ((volatile unsigned char *)data)[byte] = ((const unsigned char *)&memory->Inventory)[byte];
    data = 0;
    data = x86_object_arena_data(&memory->Arena, &temporary);
    if (!data || ((BOOT_U64)data & 4095)) kernel_panic("PERSISTENT_OBJECT_ALIGNMENT_FAILED");
    for (byte = 0; byte < temporary.Bytes; ++byte) { if (data[byte]) kernel_panic("PERSISTENT_OBJECT_NOT_ZERO"); data[byte] = 0x5a; }
    data = 0; stale = temporary;
    if (x86_object_arena_detach(&memory->Arena) || x86_region_release(&memory->Regions, &memory->Handles[1]) ||
        !x86_object_arena_release(&memory->Arena, &temporary) || x86_object_arena_data(&memory->Arena, &stale) ||
        !x86_object_arena_allocate(&memory->Arena, 4097, 4096, &temporary) || temporary.Id == stale.Id ||
        !x86_object_arena_release(&memory->Arena, &temporary) || memory->Physical.FreePages != budget ||
        memory->Arena.Objects.Count != 2 || memory->Arena.Objects.Used != sizeof(memory->Context) + sizeof(memory->Inventory))
        kernel_panic("PERSISTENT_OBJECT_LIFETIME_FAILED");
    if (!kernel_text("KERNEL_ARENA_CONTEXT: ") || !kernel_number(memory->Handles[1].Virtual + memory->ContextObject.Offset) ||
        !kernel_text("\nKERNEL_ARENA_INVENTORY: ") || !kernel_number(memory->Handles[1].Virtual + memory->InventoryObject.Offset) ||
        !kernel_text("\nKERNEL_ARENA_USED: ") || !kernel_number(memory->Arena.Objects.Used) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_ARENA_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== kernel_test_bootstrap_memory ======

void kernel_test_bootstrap_memory(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    prepare_physical_memory(memory);
    prepare_page_window(memory);
    prepare_kernel_objects(memory);
    prepare_kernel_regions(memory);
    prepare_persistent_objects(memory);
}

#include "memory.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
#ifdef KERNEL_TEST_BOOTSTRAP_MEMORY
#include "kernel/test/bootstrap/probes.h"
#endif

#ifndef KERNEL_TEST_BOOTSTRAP_MEMORY

// ====== prepare_persistent_private ======

static void prepare_persistent_private(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    unsigned char *data;
    BOOT_U32 byte;
    BOOT_U64 budget = memory->Physical.FreePages;
    if (!x86_object_arena_attach(&memory->Arena,&memory->Regions,&memory->Handles[1]) ||
        !x86_object_arena_allocate(&memory->Arena,sizeof(memory->Context),16,&memory->ContextObject) ||
        !x86_object_arena_allocate(&memory->Arena,sizeof(memory->Inventory),8,&memory->InventoryObject))
        kernel_panic("PERSISTENT_OBJECT_INIT_FAILED");
    data = x86_object_arena_data(&memory->Arena,&memory->ContextObject);
    if (!data) kernel_panic("PERSISTENT_OBJECT_ACCESS_FAILED");
    for (byte = 0; byte < sizeof(memory->Context); ++byte)
        data[byte] = ((const unsigned char *)&memory->Context)[byte];
    data = x86_object_arena_data(&memory->Arena,&memory->InventoryObject);
    if (!data) kernel_panic("PERSISTENT_OBJECT_ACCESS_FAILED");
    for (byte = 0; byte < sizeof(memory->Inventory); ++byte)
        data[byte] = ((const unsigned char *)&memory->Inventory)[byte];
    data = 0;
    if (!x86_object_arena_validate(&memory->Arena) ||
        memory->Physical.FreePages != budget || memory->Arena.Objects.Count != 2 ||
        memory->Arena.Objects.Used != sizeof(memory->Context) + sizeof(memory->Inventory))
        kernel_panic("PERSISTENT_OBJECT_LIFETIME_FAILED");
    if (!kernel_text("KERNEL_ARENA_CONTEXT: ") ||
        !kernel_number(memory->Handles[1].Virtual + memory->ContextObject.Offset) ||
        !kernel_text("\nKERNEL_ARENA_INVENTORY: ") ||
        !kernel_number(memory->Handles[1].Virtual + memory->InventoryObject.Offset) ||
        !kernel_text("\nKERNEL_ARENA_USED: ") || !kernel_number(memory->Arena.Objects.Used) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_ARENA_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}
#endif

// ====== kernel_bootstrap_memory_prepare ======

// Boot failures remain fail-stop: partially initialized owners are not discarded
// or recycled. The existing APIs validate leases, bounds, mappings and RW/NX.
void kernel_bootstrap_memory_prepare(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    if (!memory || memory->Window.Ready || memory->Arena.Ready)
        kernel_panic("BOOTSTRAP_MEMORY_ALREADY_INITIALIZED");
#ifdef KERNEL_TEST_BOOTSTRAP_MEMORY
    kernel_test_bootstrap_memory(memory);
    if (!kernel_log(0,"KERNEL_BOOTSTRAP_DIAGNOSTICS_OK")) kernel_panic("SERIAL_UNAVAILABLE");
#else
    BOOT_U64 budget;
    if (!kernel_section("allocatore fisico") ||
        !kernel_physical_initialize(&memory->Physical,memory->Context.MemoryMap,
            memory->Context.Info.MemoryMapBytes,memory->Context.Info.MemoryDescriptorBytes,x86_physical_bits()) ||
        !kernel_physical_validate(&memory->Physical)) kernel_panic("PHYSICAL_INIT_FAILED");
    if (!kernel_log(0,"KERNEL_PHYSICAL_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_section("finestra di mapping kernel") ||
        !x86_page_window_initialize(&memory->Window,&memory->Physical,memory->Context.Info.PageTableRoot) ||
        !x86_page_window_idle_valid(&memory->Window)) kernel_panic("PAGE_MAPPING_FAILED");
    if (!kernel_log(0,"KERNEL_WINDOW_READY")) kernel_panic("SERIAL_UNAVAILABLE");
    budget = memory->Physical.FreePages;
    if (!kernel_section("regione persistente RW NX") ||
        !x86_region_initialize(&memory->Regions,&memory->Physical,memory->Context.Info.PageTableRoot) ||
        !x86_region_acquire(&memory->Regions,16,16,&memory->Handles[1]) ||
        !x86_region_data(&memory->Regions,&memory->Handles[1]) || !x86_region_validate(&memory->Regions) ||
        budget < 16 || memory->Physical.FreePages != budget - 16) kernel_panic("REGION_INIT_FAILED");
    if (!kernel_log(0,"KERNEL_REGIONS_OK") || !kernel_section("oggetti persistenti kernel"))
        kernel_panic("SERIAL_UNAVAILABLE");
    prepare_persistent_private(memory);
    if (!kernel_physical_validate(&memory->Physical) || !x86_region_validate(&memory->Regions) ||
        !x86_page_window_idle_valid(&memory->Window)) kernel_panic("BOOTSTRAP_MEMORY_INVALID");
    if (!kernel_log(0,"KERNEL_BOOTSTRAP_ORDINARY_OK")) kernel_panic("SERIAL_UNAVAILABLE");
#endif
}

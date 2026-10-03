#include "boot/contract.h"
#include "mm/inventory.h"
#include "mm/physical/allocator.h"
#include "mm/objects/pool.h"
#include "diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/stack/main.h"
#include "arch/x86/mm/regions/mapping.h"
#include "arch/x86/mm/arena/objects.h"
#include "arch/x86/mm/paging/inventory.h"
#include "firmware/acpi/platform.h"
#include "firmware/acpi/madt/platform.h"
#include "arch/x86/kernel/interrupts/apic/profile.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
static KERNEL_BOOT_CONTEXT boot_context;
static KERNEL_MEMORY_INVENTORY memory_inventory;
static KERNEL_PHYSICAL_ALLOCATOR physical_allocator;
static X86_PAGE_WINDOW page_window;
static KERNEL_OBJECT_POOL object_pool;
static X86_KERNEL_MAIN_STACK main_stack;
static X86_REGION_MAPPER region_mapper;
static X86_REGION_HANDLE region_handles[2];
static X86_OBJECT_ARENA persistent_arena;
static KERNEL_OBJECT_HANDLE context_object, inventory_object;
static KERNEL_OBJECT_HANDLE paging_object;
static KERNEL_OBJECT_HANDLE acpi_object;
static KERNEL_OBJECT_HANDLE madt_object;
static X86_APIC_PROFILE apic_profile;
static X86_APIC_CAPABILITIES apic_capabilities;
__declspec(noreturn) static void kernel_continue(void);
#if defined(KERNEL_TEST_MAIN_FAULT) && KERNEL_TEST_MAIN_FAULT == 4

// ====== ritorno intenzionale soltanto nell'immagine di test isolata ===== //

static void kernel_return_probe(void)
{
    if (!kernel_log(1, "KERNEL_MAIN_RETURN_TEST_READY")) kernel_panic("SERIAL_UNAVAILABLE");
}
#endif
#ifdef KERNEL_TEST_WINDOW_FAULT
void x86_window_fault(BOOT_U64 address, BOOT_U32 mode);
#endif
#ifdef KERNEL_TEST_CPU_FAULT
void x86_cpu_fault(BOOT_U32 mode);
#endif

// ====== blocco contiguo azzerato e verifica di ogni pagina dopo riuso ===== //

static void prepare_page_window(void)
{
    BOOT_U32 index;
    BOOT_U64 pages = X86_KERNEL_WINDOW_PAGES, bytes = X86_KERNEL_WINDOW_BYTES;
    BOOT_U64 budget = physical_allocator.FreePages;
    volatile unsigned char *data;
    if (!x86_page_window_initialize(&page_window, &physical_allocator, boot_context.Info.PageTableRoot) ||
        !x86_page_window_acquire(&page_window, pages)) kernel_panic("PAGE_MAPPING_FAILED");
    data = x86_window_bytes();
    for (index = 0; index < bytes; ++index) if (data[index]) kernel_panic("PAGE_NOT_ZERO");
    for (index = 0; index < pages; ++index) {
        data[index * 4096] = 0x5a;
        data[index * 4096 + 4095] = 0xa5;
    }
    for (index = 0; index < pages; ++index)
        if (data[index * 4096] != 0x5a || data[index * 4096 + 4095] != 0xa5) kernel_panic("PAGE_WRITE_FAILED");
    if (kernel_physical_release(&physical_allocator, &page_window.Lease) ||
        !x86_page_window_release(&page_window)) kernel_panic("PAGE_LIFETIME_FAILED");
    data = 0;
    if (!x86_page_window_acquire(&page_window, pages)) kernel_panic("PAGE_REACQUIRE_FAILED");
    data = x86_window_bytes();
    for (index = 0; index < bytes; ++index) if (data[index]) kernel_panic("PAGE_REUSE_NOT_ZERO");
    if (!x86_page_window_release(&page_window)) kernel_panic("PAGE_RELEASE_FAILED");
    data = 0;
    if (physical_allocator.FreePages != budget ||
        !kernel_log(0, "KERNEL_MAPPING_OK") || !kernel_log(0, "KERNEL_WINDOW_16_PAGES_OK")) kernel_panic("PAGE_RELEASE_FAILED");
}

// ====== pool esclusivo nel mapping vivo senza reclamare la memoria del loader ===== //

static void prepare_kernel_objects(void)
{
    KERNEL_OBJECT_HANDLE first = {0}, second = {0}, stale, third = {0};
    unsigned char *data;
    BOOT_U32 index;
    BOOT_U64 budget = physical_allocator.FreePages;
    if (!x86_page_window_acquire(&page_window, X86_KERNEL_WINDOW_PAGES) ||
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
        !x86_page_window_release(&page_window) || physical_allocator.FreePages != budget) kernel_panic("OBJECT_DETACH_FAILED");
    if (!kernel_log(0, "KERNEL_OBJECTS_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== ciclo ownership fisico senza leggere o scrivere pagine non mappate ===== //

static void prepare_physical_memory(void)
{
    KERNEL_PHYSICAL_LEASE first = {0}, second = {0}, stale, reused = {0};
    BOOT_U64 total;
    if (!kernel_physical_initialize(&physical_allocator, boot_context.MemoryMap, boot_context.Info.MemoryMapBytes,
                                    boot_context.Info.MemoryDescriptorBytes, x86_physical_bits())) kernel_panic("PHYSICAL_INIT_FAILED");
    total = physical_allocator.TotalPages;
    if (!kernel_physical_allocate(&physical_allocator, 2, &first) ||
        !kernel_physical_allocate(&physical_allocator, 1, &second) || physical_allocator.FreePages != total - 3)
        kernel_panic("PHYSICAL_ALLOCATION_FAILED");
    stale = first;
    if (!kernel_physical_release(&physical_allocator, &first) || kernel_physical_release(&physical_allocator, &stale) ||
        !kernel_physical_allocate(&physical_allocator, 2, &reused) || kernel_physical_release(&physical_allocator, &stale) ||
        !kernel_physical_release(&physical_allocator, &second) || !kernel_physical_release(&physical_allocator, &reused) ||
        physical_allocator.FreePages != total || !kernel_physical_validate(&physical_allocator)) kernel_panic("PHYSICAL_RELEASE_FAILED");
    if (!kernel_physical_allocate_aligned(&physical_allocator, 4, 16, &first) ||
        !kernel_physical_allocate_aligned(&physical_allocator, 8, 512, &second) ||
        (first.Base & 65535) || (second.Base & 2097151) || physical_allocator.FreePages != total - 12 ||
        kernel_regions_overlap(first.Base, first.Pages, second.Base, second.Pages) ||
        !kernel_physical_validate(&physical_allocator)) kernel_panic("PHYSICAL_ALIGNMENT_FAILED");
    if (!kernel_text("KERNEL_PHYSICAL_64K_BASE: ") || !kernel_number(first.Base) ||
        !kernel_text("\nKERNEL_PHYSICAL_2M_BASE: ") || !kernel_number(second.Base) ||
        !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_physical_release(&physical_allocator, &first) || !kernel_physical_release(&physical_allocator, &second) ||
        physical_allocator.FreePages != total || !kernel_physical_validate(&physical_allocator)) kernel_panic("PHYSICAL_ALIGNED_RELEASE_FAILED");
    if (!kernel_log(0, "KERNEL_PHYSICAL_ALIGNED_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_log(0, "KERNEL_PHYSICAL_OK") || !kernel_text("Pagine fisiche gestite: ") ||
        !kernel_number(total) || !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== blocchi simultanei persistenti con prova di unmap riuso e vicini intatti ===== //

static void prepare_kernel_regions(void)
{
    X86_REGION_HANDLE stale, third = {0}, fourth = {0}, extra = {0};
    volatile unsigned char *first, *second;
    BOOT_U32 page, byte;
    BOOT_U64 budget = physical_allocator.FreePages;
    if (!x86_region_initialize(&region_mapper, &physical_allocator, boot_context.Info.PageTableRoot) ||
        !x86_region_acquire(&region_mapper, 32, 16, &region_handles[0]) ||
        !x86_region_acquire(&region_mapper, 16, 16, &region_handles[1])) kernel_panic("REGION_INIT_FAILED");
    first = x86_region_data(&region_mapper, &region_handles[0]);
    second = x86_region_data(&region_mapper, &region_handles[1]);
    if (!first || !second) kernel_panic("REGION_ACCESS_FAILED");
    for (page = 0; page < 32; ++page) { first[page * 4096] = 0x5a; first[page * 4096 + 4095] = 0xa5; }
    for (page = 0; page < 16; ++page) { second[page * 4096] = 0xa5; second[page * 4096 + 4095] = 0x5a; }
    first = 0; second = 0; stale = region_handles[0];
    if (!x86_region_release(&region_mapper, &region_handles[0]) || x86_region_data(&region_mapper, &stale) ||
        !x86_region_acquire(&region_mapper, 32, 16, &region_handles[0]) || region_handles[0].Id == stale.Id ||
        x86_region_release(&region_mapper, &stale)) kernel_panic("REGION_REUSE_FAILED");
    first = x86_region_data(&region_mapper, &region_handles[0]);
    second = x86_region_data(&region_mapper, &region_handles[1]);
    if (!first || !second) kernel_panic("REGION_ACCESS_FAILED");
    for (byte = 0; byte < 32 * 4096; ++byte) if (first[byte]) kernel_panic("REGION_REUSE_NOT_ZERO");
    for (page = 0; page < 16; ++page)
        if (second[page * 4096] != 0xa5 || second[page * 4096 + 4095] != 0x5a) kernel_panic("REGION_NEIGHBOR_CORRUPTED");
    first = 0; second = 0;
    if (!x86_region_acquire(&region_mapper, 1, 1, &third) || !x86_region_acquire(&region_mapper, 1, 1, &fourth) ||
        x86_region_acquire(&region_mapper, 1, 1, &extra) || !x86_region_release(&region_mapper, &third) ||
        !x86_region_release(&region_mapper, &fourth) || physical_allocator.FreePages != budget - 48 ||
        !x86_region_validate(&region_mapper)) kernel_panic("REGION_BUDGET_FAILED");
    if (!kernel_text("KERNEL_REGION_PTE: ") || !kernel_number((BOOT_U64)&x86_region_entries(3)[X86_REGION_LEAF]) ||
        !kernel_text("\nKERNEL_REGION0_PHYSICAL: ") || !kernel_number(region_mapper.Live[0].Base) ||
        !kernel_text("\nKERNEL_REGION1_PHYSICAL: ") || !kernel_number(region_mapper.Live[1].Base) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_REGIONS_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== contesto e inventario posseduti dal kernel in un mapping persistente ===== //

static void prepare_persistent_objects(void)
{
    KERNEL_OBJECT_HANDLE temporary = {0}, stale;
    unsigned char *data;
    BOOT_U32 byte;
    BOOT_U64 budget = physical_allocator.FreePages;
    if (!x86_object_arena_attach(&persistent_arena, &region_mapper, &region_handles[1]) ||
        !x86_object_arena_allocate(&persistent_arena, sizeof(boot_context), 16, &context_object) ||
        !x86_object_arena_allocate(&persistent_arena, sizeof(memory_inventory), 8, &inventory_object) ||
        !x86_object_arena_allocate(&persistent_arena, 4097, 4096, &temporary)) kernel_panic("PERSISTENT_OBJECT_INIT_FAILED");
    data = x86_object_arena_data(&persistent_arena, &context_object);
    if (!data) kernel_panic("PERSISTENT_OBJECT_ACCESS_FAILED");
    for (byte = 0; byte < sizeof(boot_context); ++byte)
        ((volatile unsigned char *)data)[byte] = ((const unsigned char *)&boot_context)[byte];
    data = 0;
    data = x86_object_arena_data(&persistent_arena, &inventory_object);
    if (!data) kernel_panic("PERSISTENT_OBJECT_ACCESS_FAILED");
    for (byte = 0; byte < sizeof(memory_inventory); ++byte)
        ((volatile unsigned char *)data)[byte] = ((const unsigned char *)&memory_inventory)[byte];
    data = 0;
    data = x86_object_arena_data(&persistent_arena, &temporary);
    if (!data || ((BOOT_U64)data & 4095)) kernel_panic("PERSISTENT_OBJECT_ALIGNMENT_FAILED");
    for (byte = 0; byte < temporary.Bytes; ++byte) { if (data[byte]) kernel_panic("PERSISTENT_OBJECT_NOT_ZERO"); data[byte] = 0x5a; }
    data = 0; stale = temporary;
    if (x86_object_arena_detach(&persistent_arena) || x86_region_release(&region_mapper, &region_handles[1]) ||
        !x86_object_arena_release(&persistent_arena, &temporary) || x86_object_arena_data(&persistent_arena, &stale) ||
        !x86_object_arena_allocate(&persistent_arena, 4097, 4096, &temporary) || temporary.Id == stale.Id ||
        !x86_object_arena_release(&persistent_arena, &temporary) || physical_allocator.FreePages != budget ||
        persistent_arena.Objects.Count != 2 || persistent_arena.Objects.Used != sizeof(boot_context) + sizeof(memory_inventory))
        kernel_panic("PERSISTENT_OBJECT_LIFETIME_FAILED");
    if (!kernel_text("KERNEL_ARENA_CONTEXT: ") || !kernel_number(region_handles[1].Virtual + context_object.Offset) ||
        !kernel_text("\nKERNEL_ARENA_INVENTORY: ") || !kernel_number(region_handles[1].Virtual + inventory_object.Offset) ||
        !kernel_text("\nKERNEL_ARENA_USED: ") || !kernel_number(persistent_arena.Objects.Used) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_ARENA_OK")) kernel_panic("SERIAL_UNAVAILABLE");
}

// ====== inizializzazione comune con pool limitato e senza servizi UEFI ===== //

__declspec(noreturn) void kernel_main(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    if (!kernel_capture_contract(info, root, entry, &boot_context)) kernel_panic("BOOT_CONTRACT_INVALID");
    if (!kernel_log(0, "KERNEL_CONTRACT_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_section("IDT eccezioni kernel") || !x86_exception_initialize()) kernel_panic("KERNEL_IDT_FAILED");
    kernel_section("inventario memoria");
    if (!kernel_inventory_memory(boot_context.MemoryMap, boot_context.Info.MemoryMapBytes,
                                 boot_context.Info.MemoryDescriptorBytes, &memory_inventory))
        kernel_panic("MEMORY_MAP_INVALID");
    if (!kernel_log(0, "KERNEL_MEMORY_OK") || !kernel_text("Pagine convenzionali candidate: ") ||
        !kernel_number(memory_inventory.ConventionalPages) || !kernel_text("\nPagine riservate: ") ||
        !kernel_number(memory_inventory.ReservedPages) || !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_section("allocatore fisico")) kernel_panic("SERIAL_UNAVAILABLE");
    prepare_physical_memory();
    if (!kernel_section("blocco kernel RW NX")) kernel_panic("SERIAL_UNAVAILABLE");
    prepare_page_window();
    if (!kernel_section("pool oggetti kernel")) kernel_panic("SERIAL_UNAVAILABLE");
    prepare_kernel_objects();
    if (!kernel_section("mapping kernel simultanei RW NX")) kernel_panic("SERIAL_UNAVAILABLE");
    prepare_kernel_regions();
    if (!kernel_section("oggetti persistenti kernel")) kernel_panic("SERIAL_UNAVAILABLE");
    prepare_persistent_objects();
    if (!kernel_section("GDT TSS e stack di emergenza kernel") ||
        !x86_kernel_segments_initialize(&physical_allocator)) kernel_panic("KERNEL_DESCRIPTORS_FAILED");
    if (!kernel_section("stack principale kernel") ||
        !x86_kernel_main_prepare(&main_stack, &physical_allocator, &boot_context.Info)) kernel_panic("MAIN_STACK_PREPARE_FAILED");
    x86_kernel_main_enter(&main_stack, kernel_continue);
}

// ====== continuazione statica senza riferimenti o ritorni sullo stack loader ===== //

__declspec(noreturn) static void kernel_continue(void)
{
    if (!x86_kernel_main_retire(&main_stack) || !x86_kernel_main_report(&main_stack)) kernel_panic("MAIN_STACK_RETIRE_FAILED");
    if (!os_acpi_root_valid(&boot_context.Info.Acpi, x86_physical_bits())) kernel_panic("ACPI_ROOT_INVALID");
    if (boot_context.Info.Acpi.Source) {
        if (!kernel_section("discovery piattaforma ACPI") ||
            !kernel_text("KERNEL_ACPI_RSDP: ") || !kernel_number(boot_context.Info.Acpi.Physical) ||
            !kernel_text("\nKERNEL_ACPI_ROOT: ") || !kernel_number(os_acpi_root_table(&boot_context.Info.Acpi, x86_physical_bits())) ||
            !kernel_text("\n") || !kernel_log(0, "KERNEL_ACPI_RSDP_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    } else if (!kernel_log(2, "ACPI_RSDP_ABSENT")) kernel_panic("SERIAL_UNAVAILABLE");
    {
        const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)x86_object_arena_data(&persistent_arena, &context_object);
        const KERNEL_MEMORY_INVENTORY *inventory = (const KERNEL_MEMORY_INVENTORY *)x86_object_arena_data(&persistent_arena, &inventory_object);
        if (!context || !inventory || !kernel_validate_contract(&context->Info, x86_kernel_root(), context->Info.KernelEntry) ||
            context->MemoryMap != boot_context.MemoryMap || inventory->ConventionalPages != memory_inventory.ConventionalPages ||
            inventory->ReservedPages != memory_inventory.ReservedPages || inventory->Regions != memory_inventory.Regions ||
            !kernel_log(0, "KERNEL_PERSISTENT_STATE_OK")) kernel_panic("PERSISTENT_STATE_INVALID");
    }
    {
        const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)x86_object_arena_data(&persistent_arena, &context_object);
        X86_PAGING_INVENTORY *paging;
        BOOT_U64 budget = physical_allocator.FreePages;
        if (!context || !kernel_section("risorse paging attive") ||
            !x86_object_arena_allocate(&persistent_arena, sizeof(*paging), 8, &paging_object)) kernel_panic("PAGING_INVENTORY_ALLOC_FAILED");
        paging = (X86_PAGING_INVENTORY *)x86_object_arena_data(&persistent_arena, &paging_object);
        if (!paging || !x86_paging_inventory_capture(context, &physical_allocator, paging) ||
            !x86_paging_inventory_verify(context, &physical_allocator, paging) ||
            physical_allocator.FreePages != budget ||
            persistent_arena.Objects.Used != sizeof(boot_context) + sizeof(memory_inventory) + sizeof(*paging))
            kernel_panic("PAGING_INVENTORY_INVALID");
        if (!kernel_text("KERNEL_PAGING_INVENTORY: ") || !kernel_number((BOOT_U64)paging) ||
            !kernel_text("\nKERNEL_PAGING_TABLES: ") || !kernel_number(paging->Count) ||
            !kernel_text("\nKERNEL_PAGING_LEAVES: ") || !kernel_number(paging->LeafPages) ||
            !kernel_text("\n") || !kernel_log(0, "KERNEL_PAGING_INVENTORY_OK")) kernel_panic("SERIAL_UNAVAILABLE");
        if (!kernel_acpi_initialize(context, &page_window, &persistent_arena, &acpi_object) ||
            !x86_paging_inventory_verify(context, &physical_allocator, paging)) kernel_panic("ACPI_DIRECTORY_INVALID");
        if (!kernel_madt_initialize(context, &page_window, &persistent_arena, &acpi_object, &madt_object) ||
            !x86_paging_inventory_verify(context, &physical_allocator, paging)) kernel_panic("MADT_TOPOLOGY_INVALID");
        if (context->Info.Acpi.Source &&
            (!x86_apic_initialize((const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&persistent_arena, &madt_object),
                                    physical_allocator.PhysicalBits, &apic_profile) ||
             !x86_paging_inventory_verify(context, &physical_allocator, paging))) kernel_panic("APIC_PREFLIGHT_INVALID");
        if (context->Info.Acpi.Source &&
            !x86_apic_probe(context, &page_window, paging,
                (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&persistent_arena, &madt_object),
                &apic_profile, &apic_capabilities)) kernel_panic("APIC_MMIO_INVALID");
    }
#ifdef KERNEL_TEST_REGION_FAULT
    {
        BOOT_U64 address = region_handles[0].Virtual;
#if KERNEL_TEST_REGION_FAULT == 1
        if (!x86_region_release(&region_mapper, &region_handles[0])) kernel_panic("REGION_FAULT_SETUP_FAILED");
#elif KERNEL_TEST_REGION_FAULT == 2
        address -= 4096;
#elif KERNEL_TEST_REGION_FAULT == 3
        address += 32 * 4096;
#elif KERNEL_TEST_REGION_FAULT == 4
        address += 31 * 4096;
        ((void (*)(void))address)();
#endif
#if KERNEL_TEST_REGION_FAULT != 4
        volatile unsigned char value = *(volatile unsigned char *)address;
        (void)value;
#endif
        kernel_panic("EXPECTED_REGION_FAULT_MISSING");
    }
#elif defined(KERNEL_TEST_MAIN_FAULT)
#if KERNEL_TEST_MAIN_FAULT == 4
    // Test del guard return mantenendo prima la normale revoca del mapping.
    x86_kernel_stack_switch(X86_KERNEL_MAIN_TOP, kernel_return_probe);
#elif KERNEL_TEST_MAIN_FAULT == 3
    ((void (*)(void))(X86_KERNEL_MAIN_TOP - 4096))();
#else
    {
        BOOT_U64 address = KERNEL_TEST_MAIN_FAULT == 1 ? X86_BOOT_STACK_VIRTUAL : X86_KERNEL_MAIN_TOP;
        volatile unsigned char value = *(volatile unsigned char *)address;
        (void)value;
    }
#endif
#if KERNEL_TEST_MAIN_FAULT != 4
    kernel_panic("EXPECTED_MAIN_FAULT_MISSING");
#endif
#elif defined(KERNEL_TEST_WINDOW_FAULT)
#if KERNEL_TEST_WINDOW_FAULT != 2
    if (!x86_page_window_acquire(&page_window, X86_KERNEL_WINDOW_PAGES)) kernel_panic("FAULT_SETUP_FAILED");
#endif
    if (!kernel_log(1, "KERNEL_WINDOW_FAULT_READY")) kernel_panic("SERIAL_UNAVAILABLE");
#if KERNEL_TEST_WINDOW_FAULT == 3
    x86_window_fault(X86_KERNEL_PAGE_WINDOW - 4096, KERNEL_TEST_WINDOW_FAULT);
#elif KERNEL_TEST_WINDOW_FAULT == 4
    x86_window_fault(X86_KERNEL_PAGE_WINDOW + X86_KERNEL_WINDOW_BYTES, KERNEL_TEST_WINDOW_FAULT);
#else
    x86_window_fault(X86_KERNEL_PAGE_WINDOW + X86_KERNEL_WINDOW_BYTES - 4096, KERNEL_TEST_WINDOW_FAULT);
#endif
    kernel_panic("EXPECTED_WINDOW_FAULT_MISSING");
#elif defined(KERNEL_TEST_CPU_FAULT)
    {
        x86_cpu_fault(KERNEL_TEST_CPU_FAULT);
        kernel_panic("EXPECTED_CPU_FAULT_MISSING");
    }
#elif defined(KERNEL_TEST_PANIC)
    kernel_panic("INTENTIONAL_TEST_PANIC");
#else
    if (!kernel_log(0, "KERNEL_READY")) kernel_panic("SERIAL_UNAVAILABLE");
    x86_kernel_stop(0x4b52454144590001ULL);
#endif
}

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
static KERNEL_BOOT_CONTEXT boot_context;
static KERNEL_MEMORY_INVENTORY memory_inventory;
static KERNEL_PHYSICAL_ALLOCATOR physical_allocator;
static X86_PAGE_WINDOW page_window;
static KERNEL_OBJECT_POOL object_pool;
static X86_KERNEL_MAIN_STACK main_stack;
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
    if (!kernel_log(0, "KERNEL_PHYSICAL_OK") || !kernel_text("Pagine fisiche gestite: ") ||
        !kernel_number(total) || !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
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
#ifdef KERNEL_TEST_MAIN_FAULT
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

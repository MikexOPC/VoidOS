#include "boot/contract.h"
#include "mm/inventory.h"
#include "mm/physical/allocator.h"
#include "diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
static KERNEL_BOOT_CONTEXT boot_context;
static KERNEL_MEMORY_INVENTORY memory_inventory;
static KERNEL_PHYSICAL_ALLOCATOR physical_allocator;

// ====== ciclo ownership fisico senza leggere o scrivere pagine non mappate ====== //

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

// ====== inizializzazione comune senza heap o servizi UEFI ====== //

__declspec(noreturn) void kernel_main(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    if (!kernel_capture_contract(info, root, entry, &boot_context)) kernel_panic("BOOT_CONTRACT_INVALID");
    if (!kernel_log(0, "KERNEL_CONTRACT_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    kernel_section("inventario memoria");
    if (!kernel_inventory_memory(boot_context.MemoryMap, boot_context.Info.MemoryMapBytes,
                                 boot_context.Info.MemoryDescriptorBytes, &memory_inventory))
        kernel_panic("MEMORY_MAP_INVALID");
    if (!kernel_log(0, "KERNEL_MEMORY_OK") || !kernel_text("Pagine convenzionali candidate: ") ||
        !kernel_number(memory_inventory.ConventionalPages) || !kernel_text("\nPagine riservate: ") ||
        !kernel_number(memory_inventory.ReservedPages) || !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_section("allocatore fisico")) kernel_panic("SERIAL_UNAVAILABLE");
    prepare_physical_memory();
#ifdef KERNEL_TEST_PANIC
    kernel_panic("INTENTIONAL_TEST_PANIC");
#else
    if (!kernel_log(0, "KERNEL_READY")) kernel_panic("SERIAL_UNAVAILABLE");
    x86_kernel_stop(0x4b52454144590001ULL);
#endif
}

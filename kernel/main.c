#include "boot/contract.h"
#include "mm/inventory.h"
#include "diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
static KERNEL_BOOT_CONTEXT boot_context;
static KERNEL_MEMORY_INVENTORY memory_inventory;

// ====== inizializzazione comune senza allocazioni o servizi UEFI ====== //

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
#ifdef KERNEL_TEST_PANIC
    kernel_panic("INTENTIONAL_TEST_PANIC");
#else
    if (!kernel_log(0, "KERNEL_READY")) kernel_panic("SERIAL_UNAVAILABLE");
    x86_kernel_stop(0x4b52454144590001ULL);
#endif
}

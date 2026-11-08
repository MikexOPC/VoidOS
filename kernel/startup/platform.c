#include "startup.h"
#include "arch/x86/kernel/cpu/percpu/identity/identity.h"
#include "arch/x86/kernel/cpu/percpu/clock/clock.h"
#include "../scheduler/kernel.h"
#include "../diagnostics/log.h"
#include "../firmware/acpi/platform.h"
#include "../firmware/acpi/madt/platform.h"
#include "arch/x86/kernel/platform.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/kernel/interrupts/apic/control/masked.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "arch/x86/mm/paging/retirement/retire.h"
#include "arch/x86/kernel/descriptors/protection.h"

#ifdef KERNEL_TEST_SYNC
#include "test/kernel/sync/integration/probe.h"
#endif
#ifdef KERNEL_TEST_K5_PRIVILEGE
#include "test/arch/x86/kernel/interrupts/privilege/probe.h"
#endif

// ====== acpi_root_private ======

// B2-B4: base scheduler profile, RSDP report, persistent arena state against the contract.
static void acpi_root_private(KERNEL_BOOTSTRAP_MEMORY *memory)
{
    if (!kernel_scheduler_run(&memory->Physical, memory->Context.Info.PageTableRoot)) kernel_panic("SCHEDULER_FAILED");
    if (!os_acpi_root_valid(&memory->Context.Info.Acpi, x86_physical_bits())) kernel_panic("ACPI_ROOT_INVALID");
    if (memory->Context.Info.Acpi.Source) {
        if (!kernel_section("discovery piattaforma ACPI") ||
            !kernel_text("KERNEL_ACPI_RSDP: ") || !kernel_number(memory->Context.Info.Acpi.Physical) ||
            !kernel_text("\nKERNEL_ACPI_ROOT: ") || !kernel_number(os_acpi_root_table(&memory->Context.Info.Acpi, x86_physical_bits())) ||
            !kernel_text("\n") || !kernel_log(0, "KERNEL_ACPI_RSDP_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    }
    else if (
        !kernel_log(2, "ACPI_RSDP_ABSENT")

    ) kernel_panic("SERIAL_UNAVAILABLE");

    const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)x86_object_arena_data(&memory->Arena, &memory->ContextObject);
    const KERNEL_MEMORY_INVENTORY *inventory = (const KERNEL_MEMORY_INVENTORY *)x86_object_arena_data(&memory->Arena, &memory->InventoryObject);

    if (!context || !inventory || !kernel_validate_contract(&context->Info, x86_kernel_root(), context->Info.KernelEntry) ||
        context->MemoryMap != memory->Context.MemoryMap || inventory->ConventionalPages != memory->Inventory.ConventionalPages ||
        inventory->ReservedPages != memory->Inventory.ReservedPages || inventory->Regions != memory->Inventory.Regions ||
        !kernel_log(0, "KERNEL_PERSISTENT_STATE_OK")
    )
    kernel_panic("PERSISTENT_STATE_INVALID");
}

// ====== paging_private ======

// B5: active paging resources captured in the arena and verified.
static X86_PAGING_INVENTORY *paging_private(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context)
{
    KERNEL_BOOTSTRAP_MEMORY *memory = startup->Memory;
    X86_PAGING_INVENTORY *paging;
    BOOT_U64 budget; if (!kernel_physical_budget_read(&memory->Physical, &budget)) kernel_panic("PHYSICAL_BUDGET_QUERY_FAILED");

    if (!context || !kernel_section("risorse paging attive") ||
        !x86_object_arena_allocate(&memory->Arena, sizeof(*paging), 8, startup->Paging)
    )
    kernel_panic("PAGING_INVENTORY_ALLOC_FAILED");

    paging = (X86_PAGING_INVENTORY *)x86_object_arena_data(&memory->Arena, startup->Paging);

    if (!paging || !x86_paging_inventory_capture(context, &memory->Physical, paging) ||
        !x86_paging_inventory_verify(context, &memory->Physical, paging) ||
        !kernel_physical_budget_matches(&memory->Physical, budget) ||
        memory->Arena.Objects.Used != sizeof(memory->Context) + sizeof(memory->Inventory) + sizeof(*paging)
    )
    kernel_panic("PAGING_INVENTORY_INVALID");

    if (!kernel_text("KERNEL_PAGING_INVENTORY: ") || !kernel_number((BOOT_U64)paging) ||
        !kernel_text("\nKERNEL_PAGING_TABLES: ") || !kernel_number(paging->Count) ||
        !kernel_text("\nKERNEL_PAGING_LEAVES: ") || !kernel_number(paging->LeafPages) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_PAGING_INVENTORY_OK")
    )
    kernel_panic("SERIAL_UNAVAILABLE");

    return paging;
}

// ====== kernel_startup_platform ======

// Phase B up to the serial IRQ: B2-B11, with the ACPI-time diagnostic hook after B6.
void kernel_startup_platform(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT **context_out, X86_PAGING_INVENTORY **paging_out)
{
    KERNEL_BOOTSTRAP_MEMORY *memory = startup->Memory;
    if (!x86_paging_retire_loader(&memory->Context, &memory->Physical)) kernel_panic("LOADER_RETIRE_FAILED");
    if (!x86_user_protection_boot()) kernel_panic("USER_PROTECTION_FAILED");
#ifdef KERNEL_TEST_K5_PRIVILEGE
    if (!x86_k5_privilege_probe(&memory->Window)) kernel_panic("K5_PRIVILEGE_DIAGNOSTIC_FAILED");
#endif
    acpi_root_private(memory);
    const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)x86_object_arena_data(&memory->Arena, &memory->ContextObject);
    X86_PAGING_INVENTORY *paging = paging_private(startup, context);

    if (!kernel_acpi_initialize(context, &memory->Window, &memory->Arena, startup->Acpi) ||
        !x86_paging_inventory_verify(context, &memory->Physical, paging)
    )
    kernel_panic("ACPI_DIRECTORY_INVALID");
    if (context->Info.Acpi.Source &&
        (!x86_percpu_wait_initialize(context, &memory->Window,
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory->Arena, startup->Acpi)) ||
        !x86_percpu_wait_report())) kernel_panic("PERCPU_WAIT_INITIALIZATION_FAILED");
    kernel_profile_after_acpi(startup, context, paging);

    if (context->Info.Acpi.Source && !x86_hpet_kernel_initialize(context, &memory->Window,
        (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory->Arena, startup->Acpi),
        paging, startup->Hpet)
    )
    kernel_panic("HPET_DISCOVERY_INVALID");

    if (!kernel_madt_initialize(context, &memory->Window, &memory->Arena, startup->Acpi, startup->Madt) ||
        !x86_paging_inventory_verify(context, &memory->Physical, paging)
    )
    kernel_panic("MADT_TOPOLOGY_INVALID");

    const KERNEL_MADT_TOPOLOGY *madt = (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&memory->Arena, startup->Madt);

    if (context->Info.Acpi.Source &&
        (
            !x86_apic_initialize(madt, memory->Physical.PhysicalBits, startup->ApicProfile) ||
            !x86_paging_inventory_verify(context, &memory->Physical, paging)
        )
    )
    kernel_panic("APIC_PREFLIGHT_INVALID");

    if (context->Info.Acpi.Source &&
        (!x86_cpu_identity_initialize(madt, startup->ApicProfile, memory->Physical.PhysicalBits) ||
        !x86_cpu_identity_report())) kernel_panic("CPU_IDENTITY_INVALID");

    if (context->Info.Acpi.Source &&
        !x86_apic_probe(context, &memory->Window, paging, madt, startup->ApicProfile, startup->ApicCapabilities)
    )
    kernel_panic("APIC_MMIO_INVALID");

    if (!x86_irq_initialize())
    kernel_panic("IRQ_RETURN_INVALID");

    if (context->Info.Acpi.Source &&
        !x86_apic_quiesce(context, &memory->Window, paging, madt, startup->ApicProfile, startup->ApicCapabilities)
    )
    kernel_panic("APIC_MASK_INVALID");

    if (context->Info.Acpi.Source &&
        !x86_uart_irq_run(context, &memory->Window, paging, madt, startup->ApicProfile, startup->ApicCapabilities)
    )
    kernel_panic("UART_IRQ_INVALID");

#ifdef KERNEL_TEST_SYNC
    kernel_sync_probe();
#endif
    *context_out = context; *paging_out = paging;
}

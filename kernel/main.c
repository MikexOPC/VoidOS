#include "bootstrap/memory.h"
#include "drivers/video/framebuffer/handoff.h"
#ifdef KERNEL_TEST_IOMMU_DISCOVERY
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/probe.h"
#endif
#ifdef KERNEL_TEST_PCI_ECAM
#include "test/arch/x86/kernel/drivers/bus/pci/ecam/integration/probe.h"
#endif
#ifdef KERNEL_TEST_IOMMU_REGISTERS
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/registers/integration/probe.h"
#endif
#ifdef KERNEL_TEST_IOMMU_DOMAIN
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/domains/integration/probe.h"
#endif
#ifdef KERNEL_TEST_IOMMU_QI
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/invalidation/integration/probe.h"
#endif
#include "drivers/video/framebuffer/mapping.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#ifdef KERNEL_TEST_VGPU_INITIALIZATION
#include "test/arch/x86/kernel/drivers/video/virtio/initialization/probe.h"
#endif
#include "scheduler/kernel.h"
#include "diagnostics/log.h"
#include "arch/x86/kernel/platform.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/stack/main.h"
#include "arch/x86/mm/paging/inventory.h"
#include "firmware/acpi/platform.h"
#include "firmware/acpi/madt/platform.h"
#include "arch/x86/kernel/interrupts/apic/profile.h"
#include "arch/x86/kernel/interrupts/apic/probe.h"
#include "arch/x86/kernel/interrupts/apic/control/masked.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "arch/x86/kernel/timer/hpet/watchdog/integration/kernel.h"
#include "arch/x86/kernel/timer/hpet/description/integration/kernel.h"

static KERNEL_BOOTSTRAP_MEMORY memory;
static X86_HPET_DESCRIPTION hpet_description;
static X86_KERNEL_MAIN_STACK main_stack;
static KERNEL_OBJECT_HANDLE paging_object;
static KERNEL_OBJECT_HANDLE acpi_object;
static KERNEL_OBJECT_HANDLE madt_object;
static X86_APIC_PROFILE apic_profile;
static X86_APIC_CAPABILITIES apic_capabilities;

#ifdef KERNEL_TEST_PS2_DECODER

int kernel_ps2_decoder_check(void);

#endif
#ifdef KERNEL_TEST_PS2_CONTROLLER

#include "arch/x86/kernel/drivers/input/ps2/integration/native.h"

#endif

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

// ====== inizializzazione comune con pool limitato e senza servizi UEFI ===== //

__declspec(noreturn) void kernel_main(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    if (!kernel_capture_contract(info, root, entry, &memory.Context)) kernel_panic("BOOT_CONTRACT_INVALID");
    if (!kernel_log(0, "KERNEL_CONTRACT_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    if (!kernel_log(0,memory.Context.Info.Gpu.Present ? "KERNEL_VGPU_PCI_PRESENT" : "KERNEL_VGPU_PCI_ABSENT"))
        kernel_panic("SERIAL_UNAVAILABLE");
    if (memory.Context.Info.Gpu.Present) {
        KERNEL_VGPU_PCI_LAYOUT layout;
        const KERNEL_VGPU_BOOT *gpu = &memory.Context.Info.Gpu;
        if (kernel_vgpu_pci_probe(gpu->Config,256,gpu->Sizes,6,52,&layout) != KERNEL_VGPU_PCI_READY)
            kernel_panic("VGPU_METADATA_INVALID");
        for (BOOT_U32 region = 0; region < 4; ++region)
            if (!kernel_text("KERNEL_VGPU_REGION: ") || !kernel_number(layout.Regions[region].Address) ||
                !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    }
    if (!kernel_section("IDT eccezioni kernel") || !x86_exception_initialize()) kernel_panic("KERNEL_IDT_FAILED");
    kernel_section("inventario memoria");
    if (!kernel_inventory_memory(memory.Context.MemoryMap, memory.Context.Info.MemoryMapBytes,
                                 memory.Context.Info.MemoryDescriptorBytes, &memory.Inventory))
        kernel_panic("MEMORY_MAP_INVALID");
    if (!fb_handoff_memory_valid(&memory.Context.Info.Framebuffer,memory.Context.MemoryMap,
        memory.Context.Info.MemoryMapBytes,memory.Context.Info.MemoryDescriptorBytes,52))
        kernel_panic("FRAMEBUFFER_RESOURCES_INVALID");
    if (!kernel_log(0,memory.Context.Info.Framebuffer.Base ?
        "KERNEL_FRAMEBUFFER_PRESENT" : "KERNEL_FRAMEBUFFER_ABSENT")) kernel_panic("SERIAL_UNAVAILABLE");
    {
        const OS_FRAMEBUFFER *fb = &memory.Context.Info.Framebuffer;
        if (!kernel_text("KERNEL_BOOT_VERSION: ") || !kernel_number(memory.Context.Info.Version) ||
            !kernel_text("\nKERNEL_BOOT_SIZE: ") || !kernel_number(memory.Context.Info.Size) ||
            !kernel_text("\nKERNEL_FB_BASE: ") || !kernel_number(fb->Base) ||
            !kernel_text("\nKERNEL_FB_CAPACITY: ") || !kernel_number(fb->Capacity) ||
            !kernel_text("\nKERNEL_FB_USED: ") || !kernel_number(fb->Used) ||
            !kernel_text("\nKERNEL_FB_WIDTH: ") || !kernel_number(fb->Width) ||
            !kernel_text("\nKERNEL_FB_HEIGHT: ") || !kernel_number(fb->Height) ||
            !kernel_text("\nKERNEL_FB_STRIDE: ") || !kernel_number(fb->Stride) ||
            !kernel_text("\nKERNEL_FB_FORMAT: ") || !kernel_number(fb->Format) ||
            !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    }
    if (!kernel_log(0, "KERNEL_MEMORY_OK") || !kernel_text("Pagine convenzionali candidate: ") ||
        !kernel_number(memory.Inventory.ConventionalPages) || !kernel_text("\nPagine riservate: ") ||
        !kernel_number(memory.Inventory.ReservedPages) || !kernel_text("\n")) kernel_panic("SERIAL_UNAVAILABLE");
    kernel_bootstrap_memory_prepare(&memory);
    {
        FB_MAPPING_GEOMETRY geometry;
        BOOT_U64 budget = memory.Physical.FreePages;
        if (!fb_mapping_geometry(&memory.Context.Info.Framebuffer,memory.Physical.PhysicalBits,&geometry) ||
            geometry.TablePages > memory.Physical.FreePages || memory.Physical.FreePages != budget)
            kernel_panic("FRAMEBUFFER_PLAN_INVALID");
        if (!kernel_text("KERNEL_FB_PLAN_PAGES: ") || !kernel_number(geometry.Pages) ||
            !kernel_text("\nKERNEL_FB_PLAN_TABLES: ") || !kernel_number(geometry.TablePages) ||
            !kernel_text("\n") || !kernel_log(0,"KERNEL_FRAMEBUFFER_PLAN_OK"))
            kernel_panic("SERIAL_UNAVAILABLE");
    }
    if (!kernel_section("GDT TSS e stack di emergenza kernel") ||
        !x86_kernel_segments_initialize(&memory.Physical)) kernel_panic("KERNEL_DESCRIPTORS_FAILED");
    if (!kernel_section("stack principale kernel") ||
        !x86_kernel_main_prepare(&main_stack, &memory.Physical, &memory.Context.Info)) kernel_panic("MAIN_STACK_PREPARE_FAILED");
    x86_kernel_main_enter(&main_stack, kernel_continue);
}

// ====== continuazione statica senza riferimenti o ritorni sullo stack loader ===== //

__declspec(noreturn) static void kernel_continue(void)
{
    if (!x86_kernel_main_retire(&main_stack) || !x86_kernel_main_report(&main_stack)) kernel_panic("MAIN_STACK_RETIRE_FAILED");
#ifdef KERNEL_TEST_PS2_DECODER
    if (!kernel_ps2_decoder_check()) kernel_panic("PS2_DECODER_ABI_FAILED");
    if (!kernel_log(0, "KERNEL_PS2_DECODER_OK")) kernel_panic("SERIAL_UNAVAILABLE");
#endif
    if (!kernel_scheduler_run(&memory.Physical, memory.Context.Info.PageTableRoot)) kernel_panic("SCHEDULER_FAILED");
    if (!os_acpi_root_valid(&memory.Context.Info.Acpi, x86_physical_bits())) kernel_panic("ACPI_ROOT_INVALID");
    if (memory.Context.Info.Acpi.Source) {
        if (!kernel_section("discovery piattaforma ACPI") ||
            !kernel_text("KERNEL_ACPI_RSDP: ") || !kernel_number(memory.Context.Info.Acpi.Physical) ||
            !kernel_text("\nKERNEL_ACPI_ROOT: ") || !kernel_number(os_acpi_root_table(&memory.Context.Info.Acpi, x86_physical_bits())) ||
            !kernel_text("\n") || !kernel_log(0, "KERNEL_ACPI_RSDP_OK")) kernel_panic("SERIAL_UNAVAILABLE");
    } else if (!kernel_log(2, "ACPI_RSDP_ABSENT")) kernel_panic("SERIAL_UNAVAILABLE");
    {
        const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)x86_object_arena_data(&memory.Arena, &memory.ContextObject);
        const KERNEL_MEMORY_INVENTORY *inventory = (const KERNEL_MEMORY_INVENTORY *)x86_object_arena_data(&memory.Arena, &memory.InventoryObject);
        if (!context || !inventory || !kernel_validate_contract(&context->Info, x86_kernel_root(), context->Info.KernelEntry) ||
            context->MemoryMap != memory.Context.MemoryMap || inventory->ConventionalPages != memory.Inventory.ConventionalPages ||
            inventory->ReservedPages != memory.Inventory.ReservedPages || inventory->Regions != memory.Inventory.Regions ||
            !kernel_log(0, "KERNEL_PERSISTENT_STATE_OK")) kernel_panic("PERSISTENT_STATE_INVALID");
    }
    {
        const KERNEL_BOOT_CONTEXT *context = (const KERNEL_BOOT_CONTEXT *)x86_object_arena_data(&memory.Arena, &memory.ContextObject);
        X86_PAGING_INVENTORY *paging;
        BOOT_U64 budget = memory.Physical.FreePages;
        if (!context || !kernel_section("risorse paging attive") ||
            !x86_object_arena_allocate(&memory.Arena, sizeof(*paging), 8, &paging_object)) kernel_panic("PAGING_INVENTORY_ALLOC_FAILED");
        paging = (X86_PAGING_INVENTORY *)x86_object_arena_data(&memory.Arena, &paging_object);
        if (!paging || !x86_paging_inventory_capture(context, &memory.Physical, paging) ||
            !x86_paging_inventory_verify(context, &memory.Physical, paging) ||
            memory.Physical.FreePages != budget ||
            memory.Arena.Objects.Used != sizeof(memory.Context) + sizeof(memory.Inventory) + sizeof(*paging))
            kernel_panic("PAGING_INVENTORY_INVALID");
        if (!kernel_text("KERNEL_PAGING_INVENTORY: ") || !kernel_number((BOOT_U64)paging) ||
            !kernel_text("\nKERNEL_PAGING_TABLES: ") || !kernel_number(paging->Count) ||
            !kernel_text("\nKERNEL_PAGING_LEAVES: ") || !kernel_number(paging->LeafPages) ||
            !kernel_text("\n") || !kernel_log(0, "KERNEL_PAGING_INVENTORY_OK")) kernel_panic("SERIAL_UNAVAILABLE");
        if (!kernel_acpi_initialize(context, &memory.Window, &memory.Arena, &acpi_object) ||
            !x86_paging_inventory_verify(context, &memory.Physical, paging)) kernel_panic("ACPI_DIRECTORY_INVALID");
#ifdef KERNEL_TEST_PCI_ECAM
        if (!x86_ecam_probe(context,&memory.Window,paging,(const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory.Arena,&acpi_object))) kernel_panic("ECAM_INVALID");
#endif
#ifdef KERNEL_TEST_IOMMU_DISCOVERY
        if (!x86_dmar_kernel_probe(context,&memory.Window,(const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory.Arena,&acpi_object)) ||
            !x86_paging_inventory_verify(context,&memory.Physical,paging)) kernel_panic("DMAR_DISCOVERY_INVALID");
#endif
#ifdef KERNEL_TEST_PS2_CONTROLLER
        if (!x86_ps2_controller_probe(context, &memory.Window,
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory.Arena, &acpi_object)))
            kernel_panic("PS2_CONTROLLER_FAILED");
#endif
        if (context->Info.Acpi.Source && !x86_hpet_kernel_initialize(context, &memory.Window,
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory.Arena, &acpi_object),
            paging, &hpet_description)) kernel_panic("HPET_DISCOVERY_INVALID");
        if (!kernel_madt_initialize(context, &memory.Window, &memory.Arena, &acpi_object, &madt_object) ||
            !x86_paging_inventory_verify(context, &memory.Physical, paging)) kernel_panic("MADT_TOPOLOGY_INVALID");
        if (context->Info.Acpi.Source &&
            (!x86_apic_initialize((const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&memory.Arena, &madt_object),
                                    memory.Physical.PhysicalBits, &apic_profile) ||
             !x86_paging_inventory_verify(context, &memory.Physical, paging))) kernel_panic("APIC_PREFLIGHT_INVALID");
        if (context->Info.Acpi.Source &&
            !x86_apic_probe(context, &memory.Window, paging,
                (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&memory.Arena, &madt_object),
                &apic_profile, &apic_capabilities)) kernel_panic("APIC_MMIO_INVALID");
        if (!x86_irq_initialize()) kernel_panic("IRQ_RETURN_INVALID");
        if (context->Info.Acpi.Source &&
            !x86_apic_quiesce(context, &memory.Window, paging,
                (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&memory.Arena, &madt_object),
                &apic_profile, &apic_capabilities)) kernel_panic("APIC_MASK_INVALID");
        if (context->Info.Acpi.Source &&
            !x86_uart_irq_run(context, &memory.Window, paging,
                (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(&memory.Arena, &madt_object),
                &apic_profile, &apic_capabilities)) kernel_panic("UART_IRQ_INVALID");
#if defined(KERNEL_TEST_IOMMU_REGISTERS)
        if (!x86_vtd_kernel_probe(context,&memory.Window,x86_uart_lapic_mapping(),x86_dmar_kernel_information(),paging))
            kernel_panic("VTD_REGISTERS_INVALID");
#ifdef KERNEL_TEST_IOMMU_QI
        if (!x86_qi_probe(context,&memory.Window,x86_uart_lapic_mapping(),paging,x86_dmar_kernel_information(),
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory.Arena,&acpi_object))) kernel_panic("QI_INVALID");
#elif defined(KERNEL_TEST_IOMMU_DOMAIN)
        if (!x86_vtd_domain_probe(context,&memory.Window,x86_uart_lapic_mapping(),paging,x86_dmar_kernel_information()))
            kernel_panic("VTD_DOMAIN_INVALID");
#endif
#elif defined(KERNEL_TEST_VGPU_INITIALIZATION)
        if (!context->Info.Acpi.Source || !x86_vgpu_initialization_probe(context,&memory.Window,
            x86_uart_lapic_mapping(),paging,
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory.Arena,&acpi_object)))
            kernel_panic("VGPU_INITIALIZATION_FAILED");
#else
#if defined(KERNEL_TEST_CONSOLE_ACTIVE) || defined(KERNEL_CONSOLE_SHELL)
        if (context->Info.Acpi.Source && !x86_fb_kernel_start(context, &memory.Window,
            x86_uart_lapic_mapping(), paging)) kernel_panic("FRAMEBUFFER_MAPPING_FAILED");
        if (context->Info.Acpi.Source && !x86_vgpu_kernel_start(context,&memory.Window,
            x86_uart_lapic_mapping(),paging)) kernel_panic("VGPU_MAPPING_FAILED");
#endif
        if (context->Info.Acpi.Source &&
            !x86_watchdog_kernel_start(context,&memory.Window,&memory.Arena,&acpi_object,&madt_object,
                paging,&apic_profile,&apic_capabilities,&hpet_description)) kernel_panic("LAPIC_TIMER_INVALID");
#if !defined(KERNEL_TEST_CONSOLE_ACTIVE) && !defined(KERNEL_CONSOLE_SHELL)
        if (context->Info.Acpi.Source && !x86_fb_kernel_start(context, &memory.Window,
            x86_uart_lapic_mapping(), paging)) kernel_panic("FRAMEBUFFER_MAPPING_FAILED");
        if (context->Info.Acpi.Source && !x86_vgpu_kernel_start(context,&memory.Window,
            x86_uart_lapic_mapping(),paging)) kernel_panic("VGPU_MAPPING_FAILED");
#else
        if (!x86_fb_kernel_inventory_verify(context, &memory.Physical,
            x86_uart_lapic_mapping(), paging)) kernel_panic("CONSOLE_ACTIVE_RESOURCES_FAILED");
        if (!kernel_log(0, "KERNEL_CONSOLE_ACTIVE_RESOURCES_OK")) kernel_panic("CONSOLE_ACTIVE_LOG_FAILED");
#endif
#endif
    }
#ifdef KERNEL_TEST_REGION_FAULT
    {
        BOOT_U64 address = memory.Handles[0].Virtual;
#if KERNEL_TEST_REGION_FAULT == 1
        if (!x86_region_release(&memory.Regions, &memory.Handles[0])) kernel_panic("REGION_FAULT_SETUP_FAILED");
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
    if (!x86_page_window_acquire(&memory.Window, X86_KERNEL_WINDOW_PAGES)) kernel_panic("FAULT_SETUP_FAILED");
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

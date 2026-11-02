#include "startup.h"
#include "../diagnostics/log.h"
#include "arch/x86/mm/window/access.h"
#include "arch/x86/kernel/stack/main.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#ifdef KERNEL_TEST_AP_STRESS_LOCK
#include "test/arch/x86/kernel/cpu/percpu/panic/integration/hold.h"
#endif
#ifdef KERNEL_TEST_AP_STRESS_BARRIER
#include "test/arch/x86/kernel/cpu/percpu/panic/concurrency/barrier.h"
#endif

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
#ifdef KERNEL_TEST_VGPU_INITIALIZATION
#include "test/arch/x86/kernel/drivers/video/virtio/initialization/probe.h"
#endif
#ifdef KERNEL_TEST_PS2_DECODER
int kernel_ps2_decoder_check(void);
#endif
#ifdef KERNEL_TEST_PS2_CONTROLLER
#include "arch/x86/kernel/drivers/input/ps2/integration/native.h"
#endif
#ifdef KERNEL_TEST_WINDOW_FAULT
void x86_window_fault(BOOT_U64 address, BOOT_U32 mode);
#endif
#ifdef KERNEL_TEST_CPU_FAULT
void x86_cpu_fault(BOOT_U32 mode);
#endif

// ====== kernel_profile_after_retire ======

void kernel_profile_after_retire(void)
{
#ifdef KERNEL_TEST_PS2_DECODER
    if (!kernel_ps2_decoder_check()) kernel_panic("PS2_DECODER_ABI_FAILED");
    if (!kernel_log(0, "KERNEL_PS2_DECODER_OK")) kernel_panic("SERIAL_UNAVAILABLE");
#endif
}

// ====== kernel_profile_after_acpi ======

// After B6: diagnostics that only need the ACPI directory (ECAM/topology/routing/activation, DMAR, PS/2).
void kernel_profile_after_acpi(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging)
{
    (void)startup; (void)context; (void)paging;
#if defined(KERNEL_TEST_PCI_ECAM) || defined(KERNEL_TEST_IOMMU_DISCOVERY) || defined(KERNEL_TEST_PS2_CONTROLLER)
    KERNEL_BOOTSTRAP_MEMORY *memory = startup->Memory;
    const KERNEL_ACPI_DIRECTORY *directory = (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory->Arena, startup->Acpi);
#endif
#ifdef KERNEL_TEST_PCI_ECAM
    if (!x86_ecam_probe(context,&memory->Window,paging,directory)) kernel_panic("ECAM_INVALID");
#endif
#ifdef KERNEL_TEST_IOMMU_DISCOVERY
    if (!x86_dmar_kernel_probe(context,&memory->Window,directory) ||
        !x86_paging_inventory_verify(context,&memory->Physical,paging)) kernel_panic("DMAR_DISCOVERY_INVALID");
#endif
#ifdef KERNEL_TEST_PS2_CONTROLLER
    if (!x86_ps2_controller_probe(context, &memory->Window, directory)) kernel_panic("PS2_CONTROLLER_FAILED");
#endif
}

// ====== kernel_profile_devices ======

// B12 replacement for the IOMMU register/domain/QI and VirtIO initialization diagnostics.
int kernel_profile_devices(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging)
{
    (void)startup; (void)context; (void)paging;

    #if defined(KERNEL_TEST_IOMMU_REGISTERS)
        KERNEL_BOOTSTRAP_MEMORY *memory = startup->Memory;
        if (!x86_vtd_kernel_probe(context,&memory->Window,x86_uart_lapic_mapping(),x86_dmar_kernel_information(),paging))
            kernel_panic("VTD_REGISTERS_INVALID");
    #ifdef KERNEL_TEST_IOMMU_QI
        if (!x86_qi_probe(context,&memory->Window,x86_uart_lapic_mapping(),paging,x86_dmar_kernel_information(),
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory->Arena,startup->Acpi))) kernel_panic("QI_INVALID");
    #elif defined(KERNEL_TEST_IOMMU_DOMAIN)
        if (!x86_vtd_domain_probe(context,&memory->Window,x86_uart_lapic_mapping(),paging,x86_dmar_kernel_information()))
            kernel_panic("VTD_DOMAIN_INVALID");
    #endif
        return 1;
    #elif defined(KERNEL_TEST_VGPU_INITIALIZATION)
        KERNEL_BOOTSTRAP_MEMORY *memory = startup->Memory;
        if (!context->Info.Acpi.Source || !x86_vgpu_initialization_probe(context,&memory->Window,
            x86_uart_lapic_mapping(),paging,
            (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&memory->Arena,startup->Acpi)))
            kernel_panic("VGPU_INITIALIZATION_FAILED");
        return 1;
    #else
        return 0;
    #endif
}

#if defined(KERNEL_TEST_MAIN_FAULT) && KERNEL_TEST_MAIN_FAULT == 4
// ====== kernel_return_probe ======

// Intentional return, only in the isolated test image.
static void kernel_return_probe(void) {
    if (!kernel_log(1, "KERNEL_MAIN_RETURN_TEST_READY")) kernel_panic("SERIAL_UNAVAILABLE");
}
#endif

// ====== kernel_profile_finish ======

// B13: KERNEL_READY and stop, or the intentional fault of an isolated test image.
__declspec(noreturn) void kernel_profile_finish(const KERNEL_STARTUP *startup)
{
    (void)startup;
    #ifdef KERNEL_TEST_REGION_FAULT
    {
        BOOT_U64 address = startup->Memory->Handles[0].Virtual;
        #if KERNEL_TEST_REGION_FAULT == 1
                if (!x86_region_release(&startup->Memory->Regions, &startup->Memory->Handles[0])) kernel_panic("REGION_FAULT_SETUP_FAILED");
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
        if (!x86_page_window_acquire(&startup->Memory->Window, X86_KERNEL_WINDOW_PAGES)) kernel_panic("FAULT_SETUP_FAILED");
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
        x86_cpu_fault(KERNEL_TEST_CPU_FAULT);
        kernel_panic("EXPECTED_CPU_FAULT_MISSING");
    #elif defined(KERNEL_TEST_AP_STRESS_LOCK)
        xp_panic_group_lock_probe();
    #elif defined(KERNEL_TEST_AP_STRESS_BARRIER)
        xp_stress_start();
    #elif defined(KERNEL_TEST_PANIC)
        kernel_panic("INTENTIONAL_TEST_PANIC");
    #else
        if (!kernel_log(0, "KERNEL_READY")) kernel_panic("SERIAL_UNAVAILABLE");
        x86_kernel_stop(0x4b52454144590001ULL);
    #endif
}

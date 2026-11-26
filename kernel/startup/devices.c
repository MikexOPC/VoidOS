#include "startup.h"
#ifdef KERNEL_TEST_SMP_PANIC
void xp_panic_integration_probe(void);
#endif
#include "arch/x86/kernel/cpu/percpu/resources/resources.h"
#include "arch/x86/kernel/cpu/percpu/transition/native.h"
#include "../diagnostics/log.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/virtio/iommu/session.h"
#include "arch/x86/mm/arena/objects.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "arch/x86/kernel/timer/hpet/watchdog/integration/kernel.h"
#include "arch/x86/mm/heap/binding.h"

// ====== video_private ======

static void video_private(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging)
{
#if KERNEL_TEST_AP_SERVICE == 1 || KERNEL_TEST_AP_SERVICE == 6
    (void)startup; (void)context; (void)paging;
#else
    if (context->Info.Acpi.Source && !x86_fb_kernel_start(context, &startup->Memory->Window,
        x86_uart_lapic_mapping(), paging)) kernel_panic("FRAMEBUFFER_MAPPING_FAILED");
    if (context->Info.Acpi.Source && !x86_vgpu_kernel_start(context,&startup->Memory->Window,
        x86_uart_lapic_mapping(),paging)) kernel_panic("VGPU_MAPPING_FAILED");
    if (context->Info.Acpi.Source && !x86_vgpu_kernel_session_start(
        (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(&startup->Memory->Arena, startup->Acpi)))
    kernel_panic("VGPU_SESSION_FAILED");
#endif
}

// ====== kernel_startup_devices ======

// B12. A diagnostic hook may replace the whole step. Otherwise the console profiles map the video
// before the LAPIC timer, because the input session runs inside the watchdog start and needs it.
void kernel_startup_devices(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging)
{
    if (context->Info.Acpi.Source && !x86_ap_resources_start(context, &startup->Memory->Window,
        x86_uart_lapic_mapping(), paging)) kernel_panic("AP_RESOURCES_FAILED");

#ifdef KERNEL_TEST_SMP_PANIC
    xp_panic_integration_probe();
#endif
    #if KERNEL_TEST_AP_SERVICE != 1 && KERNEL_TEST_AP_SERVICE != 6
    // G3: the kernel heap is bound first, in every profile, so the framebuffer and later budgets
    // already include it.
    if (context->Info.Acpi.Source && !x86_heap_kernel_start(context, &startup->Memory->Window,
        x86_uart_lapic_mapping(), paging)) kernel_panic("KERNEL_HEAP_FAILED");
    #endif

    if (context->Info.Acpi.Source && !x86_ap_transition_start(context, &startup->Memory->Window,
        x86_uart_lapic_mapping(), paging)) kernel_panic("AP_TRANSITION_TABLES_FAILED");

    if (kernel_profile_devices(startup, context, paging)) return;

    #if defined(KERNEL_TEST_CONSOLE_ACTIVE) || defined(KERNEL_CONSOLE_SHELL)
        video_private(startup, context, paging);

    #endif

        if (context->Info.Acpi.Source &&
            !x86_watchdog_kernel_start(context,&startup->Memory->Window,&startup->Memory->Arena,startup->Acpi,startup->Madt,
                paging,startup->ApicProfile,startup->ApicCapabilities,startup->Hpet)
        )
        kernel_panic("LAPIC_TIMER_INVALID");

    #if !defined(KERNEL_TEST_CONSOLE_ACTIVE) && !defined(KERNEL_CONSOLE_SHELL)
        video_private(startup, context, paging);
    #else
        if (!x86_vgpu_kernel_session_stop()) kernel_panic("VGPU_SESSION_RETAINED");
        if (!x86_fb_kernel_inventory_verify(context, &startup->Memory->Physical,
            x86_uart_lapic_mapping(), paging)
        )
        kernel_panic("CONSOLE_ACTIVE_RESOURCES_FAILED");

        if (!kernel_log(0, "KERNEL_CONSOLE_ACTIVE_RESOURCES_OK")) kernel_panic("CONSOLE_ACTIVE_LOG_FAILED");
    #endif
}

#include "startup.h"
#include "../diagnostics/log.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "arch/x86/kernel/timer/hpet/watchdog/integration/kernel.h"

// ====== video_private ======

static void video_private(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging)
{
    if (context->Info.Acpi.Source && !x86_fb_kernel_start(context, &startup->Memory->Window,
        x86_uart_lapic_mapping(), paging)) kernel_panic("FRAMEBUFFER_MAPPING_FAILED");
    if (context->Info.Acpi.Source && !x86_vgpu_kernel_start(context,&startup->Memory->Window,
        x86_uart_lapic_mapping(),paging)) kernel_panic("VGPU_MAPPING_FAILED");
}

// ====== kernel_startup_devices ======

// B12. A diagnostic hook may replace the whole step. Otherwise the console profiles map the video
// before the LAPIC timer, because the input session runs inside the watchdog start and needs it.
void kernel_startup_devices(const KERNEL_STARTUP *startup, const KERNEL_BOOT_CONTEXT *context, X86_PAGING_INVENTORY *paging)
{
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
        if (!x86_fb_kernel_inventory_verify(context, &startup->Memory->Physical,
            x86_uart_lapic_mapping(), paging)
        )
        kernel_panic("CONSOLE_ACTIVE_RESOURCES_FAILED");

        if (!kernel_log(0, "KERNEL_CONSOLE_ACTIVE_RESOURCES_OK")) kernel_panic("CONSOLE_ACTIVE_LOG_FAILED");
    #endif
}

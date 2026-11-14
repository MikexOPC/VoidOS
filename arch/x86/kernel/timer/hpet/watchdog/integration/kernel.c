#include "kernel.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "arch/x86/kernel/serial/interrupts/route.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_USER_PROCESS
#include "kernel/process/boot.h"
#endif
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#endif

// ====== x86_watchdog_kernel_start ======

int x86_watchdog_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_OBJECT_ARENA *arena, const KERNEL_OBJECT_HANDLE *acpi, const KERNEL_OBJECT_HANDLE *madt,
    const X86_PAGING_INVENTORY *inventory, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_HPET_DESCRIPTION *description)
{
    X86_WATCHDOG_ENV env;
    const KERNEL_ACPI_DIRECTORY *directory;
    env.Context = context; env.Window = window; env.Inventory = inventory;
    env.Mapping = x86_uart_lapic_mapping(); env.Profile = profile;
    env.Capabilities = caps; env.Description = description;
    env.Topology = (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(arena,madt);
    directory = (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(arena,acpi);
    if (!directory || !env.Topology || !x86_timer_run(context,window,directory,env.Mapping,inventory,&env)) return 0;
#ifdef KERNEL_USER_PROCESS
    if (x86_timer_ready() && !kernel_process_boot(window,x86_timer_owner())) kernel_panic("PROCESS_BOOT_FAILED");
#endif
#ifdef KERNEL_PS2_INPUT
    // A live or unconfirmed source retains its owners; never free its mapping.
    if (x86_timer_ready() && !x86_ps2_session_run(&env,&x86_timer_owner()->Clock,directory)) kernel_panic("PS2_INPUT_FAILED");
#endif
    return x86_timer_finish();
}

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

// ====== x86_watchdog_kernel_prepare ======

int x86_watchdog_kernel_prepare(X86_WATCHDOG_KERNEL_RUNTIME *runtime,
    const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_OBJECT_ARENA *arena, const KERNEL_OBJECT_HANDLE *acpi, const KERNEL_OBJECT_HANDLE *madt,
    const X86_PAGING_INVENTORY *inventory, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_HPET_DESCRIPTION *description)
{
    X86_WATCHDOG_ENV *env = &runtime->Environment;
    env->Context = context;
    env->Window = window;
    env->Inventory = inventory;
    env->Mapping = x86_uart_lapic_mapping();
    env->Profile = profile;
    env->Capabilities = caps;
    env->Description = description;
    env->Topology = (const KERNEL_MADT_TOPOLOGY *)x86_object_arena_data(arena,madt);
    runtime->Directory = (const KERNEL_ACPI_DIRECTORY *)x86_object_arena_data(arena,acpi);
    return runtime->Directory && env->Topology &&
        x86_timer_run(context,window,runtime->Directory,env->Mapping,inventory,env);
}

// ====== x86_watchdog_kernel_dispatch ======

void x86_watchdog_kernel_dispatch(const X86_WATCHDOG_KERNEL_RUNTIME *runtime)
{
    (void)runtime;
#ifdef KERNEL_USER_PROCESS
    if (x86_timer_ready() &&
        !kernel_process_boot(runtime->Environment.Window,x86_timer_owner())
    )
    kernel_panic("PROCESS_BOOT_FAILED");
#endif
#ifdef KERNEL_PS2_INPUT
    if (x86_timer_ready() &&
        !x86_ps2_session_run(&runtime->Environment,&x86_timer_owner()->Clock,runtime->Directory)
    )
    kernel_panic("PS2_INPUT_FAILED");
#endif
}

// ====== x86_watchdog_kernel_start ======

int x86_watchdog_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_OBJECT_ARENA *arena, const KERNEL_OBJECT_HANDLE *acpi, const KERNEL_OBJECT_HANDLE *madt,
    const X86_PAGING_INVENTORY *inventory, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_HPET_DESCRIPTION *description)
{
    X86_WATCHDOG_KERNEL_RUNTIME runtime;
    if (!x86_watchdog_kernel_prepare(&runtime,context,window,arena,acpi,madt,
        inventory,profile,caps,description)
    )
    return 0;

    x86_watchdog_kernel_dispatch(&runtime);
    return x86_timer_finish();
}

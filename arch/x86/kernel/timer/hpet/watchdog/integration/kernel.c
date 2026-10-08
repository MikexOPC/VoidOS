#include "kernel.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "arch/x86/kernel/serial/interrupts/route.h"

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
    return directory && env.Topology && x86_timer_run(context,window,directory,env.Mapping,inventory,&env);
}

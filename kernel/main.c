#include "startup/startup.h"
#include "diagnostics/log.h"
#include "arch/x86/kernel/descriptors/segments.h"
#include "arch/x86/kernel/stack/main.h"

// Boot sequence only; each step and its prerequisites: kernel/documentation/main.md.
static KERNEL_BOOTSTRAP_MEMORY memory;
static X86_HPET_DESCRIPTION hpet_description;
static X86_KERNEL_MAIN_STACK main_stack;
static KERNEL_OBJECT_HANDLE paging_object;
static KERNEL_OBJECT_HANDLE acpi_object;
static KERNEL_OBJECT_HANDLE madt_object;
static X86_APIC_PROFILE apic_profile;
static X86_APIC_CAPABILITIES apic_capabilities;
static const KERNEL_STARTUP startup = {&memory, &hpet_description, &paging_object, &acpi_object, &madt_object,
    &apic_profile, &apic_capabilities};

__declspec(noreturn) static void kernel_continue(void);

// ====== inizializzazione comune con pool limitato e senza servizi UEFI ===== //

__declspec(noreturn) void kernel_main(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    kernel_startup_early(&memory, info, root, entry);
    if (!kernel_section("GDT TSS e stack di emergenza kernel") ||
        !x86_kernel_segments_initialize(&memory.Physical)
    ) 
    kernel_panic("KERNEL_DESCRIPTORS_FAILED");
    if (!kernel_section("stack principale kernel") ||
        !x86_kernel_main_prepare(&main_stack, &memory.Physical, &memory.Context.Info)) kernel_panic("MAIN_STACK_PREPARE_FAILED");
    x86_kernel_main_enter(&main_stack, kernel_continue);
}

// ====== continuazione statica senza riferimenti o ritorni sullo stack loader ===== //

__declspec(noreturn) static void kernel_continue(void) {
    const KERNEL_BOOT_CONTEXT *context;
    X86_PAGING_INVENTORY *paging;
    if (!x86_kernel_main_retire(&main_stack) || !x86_kernel_main_report(&main_stack)) kernel_panic("MAIN_STACK_RETIRE_FAILED");
    kernel_profile_after_retire();
    kernel_startup_platform(&startup, &context, &paging);
    kernel_startup_devices(&startup, context, paging);
    kernel_profile_finish(&startup);
}

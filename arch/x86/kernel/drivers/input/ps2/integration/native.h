#ifndef X86_PS2_NATIVE_H
#define X86_PS2_NATIVE_H
#include "arch/x86/mm/window/page.h"
#include "kernel/firmware/acpi/root.h"
int x86_ps2_controller_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const KERNEL_ACPI_DIRECTORY *directory);
#endif

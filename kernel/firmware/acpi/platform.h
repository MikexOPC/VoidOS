#ifndef OS_KERNEL_ACPI_PLATFORM_H
#define OS_KERNEL_ACPI_PLATFORM_H
#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/arena/objects.h"
int kernel_acpi_initialize(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
                           X86_OBJECT_ARENA *arena, KERNEL_OBJECT_HANDLE *output);
#endif

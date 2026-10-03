#ifndef OS_KERNEL_MADT_PLATFORM_H
#define OS_KERNEL_MADT_PLATFORM_H
#include "kernel/firmware/acpi/platform.h"
int kernel_madt_initialize(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window, X86_OBJECT_ARENA *arena,
                           const KERNEL_OBJECT_HANDLE *directory, KERNEL_OBJECT_HANDLE *output);
#endif

#include <stdlib.h>
#include "kernel/firmware/acpi/platform.h"
#include "kernel/diagnostics/log.h"

/* Link-only traps for kernel_acpi_initialize, which this host fixture
   never calls. Real directory validation/capture code is linked unchanged. */

// ====== unavailable_window ======

int x86_page_window_read_acpi(X86_PAGE_WINDOW *window, const KERNEL_BOOT_CONTEXT *context,
    BOOT_U64 physical, BOOT_U64 bytes, unsigned char *output)
{
    (void)window; (void)context; (void)physical; (void)bytes; (void)output;
    abort();
}

// ====== unavailable_arena_allocate ======

int x86_object_arena_allocate(X86_OBJECT_ARENA *owner, BOOT_U64 bytes, BOOT_U64 alignment,
    KERNEL_OBJECT_HANDLE *output)
{
    (void)owner; (void)bytes; (void)alignment; (void)output; abort();
}

// ====== unavailable_arena_data ======

unsigned char *x86_object_arena_data(const X86_OBJECT_ARENA *owner, const KERNEL_OBJECT_HANDLE *handle)
{
    (void)owner; (void)handle; abort();
}

// ====== unavailable_arena_release ======

int x86_object_arena_release(X86_OBJECT_ARENA *owner, KERNEL_OBJECT_HANDLE *handle)
{
    (void)owner; (void)handle; abort();
}

// ====== unavailable_text ======

int kernel_text(const char *text) { (void)text; abort(); }

// ====== unavailable_section ======

int kernel_section(const char *name) { (void)name; abort(); }

// ====== unavailable_log ======

int kernel_log(unsigned int level, const char *message) { (void)level; (void)message; abort(); }

// ====== unavailable_number ======

int kernel_number(BOOT_U64 value) { (void)value; abort(); }

#include "clock.h"
#include "arch/x86/mm/window/page.h"

typedef struct {
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW  *Window;
} PM_READER;

// ====== read_firmware_private ======

static int read_firmware_private(void *context, BOOT_U64 physical, BOOT_U32 bytes, unsigned char *output) {
    const PM_READER *reader = context;
    return x86_page_window_read_acpi(reader->Window, reader->Context, physical, bytes, output);
}

// ====== x86_percpu_wait_initialize ======

int x86_percpu_wait_initialize(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const KERNEL_ACPI_DIRECTORY *directory) {
    X86_PM_CLOCK source = {0};
    PM_READER reader = {context, window};
    if (!context || !window || !window->Pool || !directory ||
        !x86_pm_clock_capture(directory, window->Pool->PhysicalBits, read_firmware_private, &reader, &source) ||
        !x86_page_window_idle_valid(window)) return 0;
    // Bring-up policy: 100 ms or one million polls, whichever comes first; not a performance target.
    return x86_percpu_wait_bind(&source, (KERNEL_PM_TIMER_HZ + 9U) / 10U, 1000000U);
}

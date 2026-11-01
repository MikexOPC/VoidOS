#include "kernel.h"
#include "kernel/diagnostics/log.h"
#include "../probe/probe.h"

typedef struct {
    const KERNEL_BOOT_CONTEXT  *Context;
    X86_PAGE_WINDOW           *Window;
} HPET_READER;

_Static_assert(X86_HPET_DIRECTORY_LIMIT == KERNEL_ACPI_ROOT_ENTRIES,
    "HPET uses the bounded ACPI directory interface");

// ====== separated ======

static int separated(const void *input, size_t length,
    const X86_HPET_DESCRIPTION *output)
{
    uintptr_t start = (uintptr_t)input;
    uintptr_t target = (uintptr_t)output;
    if (!input || !output || target % _Alignof(X86_HPET_DESCRIPTION) ||
        start > UINTPTR_MAX - length || target > UINTPTR_MAX - sizeof(*output)) {
        return 0;
    }
    return start + length <= target || target + sizeof(*output) <= start;
}

// ====== read_firmware ======

/* The existing page-window reader validates ACPI memory ownership and copies
 * through its temporary mapping. No physical pointer or new register access. */
static int read_firmware(void *context, uint64_t physical, uint32_t bytes,
    unsigned char *output)
{
    HPET_READER *reader = context;
    return x86_page_window_read_acpi(reader->Window, reader->Context,
        physical, bytes, output);
}

// ====== capture ======

static int capture(const KERNEL_BOOT_CONTEXT *context,
    X86_PAGE_WINDOW *window, const KERNEL_ACPI_DIRECTORY *directory,
    X86_HPET_DESCRIPTION *output, const X86_PAGING_INVENTORY *inventory)
{
    X86_HPET_DESCRIPTION draft;
    HPET_READER reader = {context, window};
    KERNEL_PHYSICAL_ALLOCATOR *pool;
    BOOT_U64 free_pages;
    BOOT_U64 root;
    int status;
    if (!separated(context, sizeof(*context), output) ||
        !separated(window, sizeof(*window), output) ||
        !separated(directory, sizeof(*directory), output) ||
        !window->Pool || !separated(window->Pool, sizeof(*window->Pool), output) ||
        !x86_page_window_idle_valid(window) ||
        window->Root != context->Info.PageTableRoot) {
        return 0;
    }
    pool = window->Pool;
    if (!kernel_acpi_directory_valid(directory, pool->PhysicalBits)) {
        return 0;
    }
    if (!kernel_physical_budget_read(pool, &free_pages)) return 0;
    root = window->Root;
    status = x86_hpet_description_find(directory->Entries, directory->Count,
        pool->PhysicalBits, read_firmware, &reader, &draft);
    /* Verify cleanup even on read failure before any result is published.
     * These checks are resource accounting, not a leak detector for hardware. */
    if (window->Pool != pool || window->Root != root ||
        !kernel_physical_budget_matches(pool, free_pages) || !x86_page_window_idle_valid(window)) {
        return 0;
    }
    if (status == X86_HPET_ABSENT) {
        return kernel_log(2, "KERNEL_HPET_ABSENT");
    }
    if (status != X86_HPET_FOUND) {
        return 0;
    }
    if (inventory && !x86_hpet_probe(context, window, inventory, &draft)) { return 0; }
    if (!kernel_section("discovery HPET dalla directory ACPI") ||
        !kernel_text("KERNEL_HPET_PHYSICAL: ") || !kernel_number(draft.Physical) ||
        !kernel_text("\nKERNEL_HPET_BLOCK_ID: ") || !kernel_number(draft.BlockId) ||
        !kernel_text("\nKERNEL_HPET_NUMBER: ") || !kernel_number(draft.Number) ||
        !kernel_text("\nKERNEL_HPET_DESCRIPTION: ") || !kernel_number((BOOT_U64)output) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_HPET_DISCOVERY_OK")) {
        return 0;
    }
    *output = draft;
    return 1;
}

// ====== x86_hpet_kernel_capture ======

int x86_hpet_kernel_capture(const KERNEL_BOOT_CONTEXT *context,
    X86_PAGE_WINDOW *window, const KERNEL_ACPI_DIRECTORY *directory,
    X86_HPET_DESCRIPTION *output)
{
    return capture(context, window, directory, output, NULL);
}

// ====== x86_hpet_kernel_initialize ======

int x86_hpet_kernel_initialize(const KERNEL_BOOT_CONTEXT *context,
    X86_PAGE_WINDOW *window, const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PAGING_INVENTORY *inventory, X86_HPET_DESCRIPTION *output)
{
    return inventory && separated(inventory, sizeof(*inventory), output) &&
        capture(context, window, directory, output, inventory);
}

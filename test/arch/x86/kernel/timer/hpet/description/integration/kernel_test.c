#include "../../../../../../../../arch/x86/kernel/timer/hpet/description/integration/kernel.h"
#include "kernel/diagnostics/log.h"

static KERNEL_BOOT_CONTEXT boot;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGE_WINDOW window;
static KERNEL_ACPI_DIRECTORY directory;
static unsigned char table[56];
static unsigned reads;
static unsigned fault;

// ====== x86_hpet_probe ======

/* Adapter suite isolates the probe. Separate probe suite uses real C policy
 * and MMIO callback with mocked CPU/mapping services; QEMU checks real MMIO. */
int x86_hpet_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *owner,
    const X86_PAGING_INVENTORY *inventory, const X86_HPET_DESCRIPTION *description)
{
    return context && owner && inventory && description && fault != 10;
}

// ====== x86_page_window_idle_valid ======

int x86_page_window_idle_valid(const X86_PAGE_WINDOW *owner)
{
    return owner == &window && owner->Ready && !owner->Active;
}

// ====== kernel_acpi_directory_valid ======

int kernel_acpi_directory_valid(const KERNEL_ACPI_DIRECTORY *owner, OS_ACPI_U32 bits)
{
    return owner == &directory && owner->Count == 1 && bits == 48 && fault != 1;
}

// ====== x86_page_window_read_acpi ======

int x86_page_window_read_acpi(X86_PAGE_WINDOW *owner, const KERNEL_BOOT_CONTEXT *context,
    BOOT_U64 physical, BOOT_U64 bytes, unsigned char *output)
{
    unsigned index;
    ++reads;
    if (owner != &window || context != &boot || physical != 0x10000 ||
        (bytes != 4 && bytes != 56) || fault == 2) { return 0; }
    if (fault == 3) { window.Active = 1; }
    if (fault == 4) { --pool.FreePages; }
    if (fault == 5) { ++window.Root; }
    for (index = 0; index < bytes; ++index) { output[index] = table[index]; }
    return 1;
}

// ====== kernel_section ======

int kernel_section(const char *name) { return name && fault != 6; }

// ====== kernel_text ======

int kernel_text(const char *text) { return text && fault != 7; }

// ====== kernel_number ======

int kernel_number(BOOT_U64 number) { (void)number; return fault != 8; }

// ====== kernel_log ======

int kernel_log(unsigned int level, const char *message)
{
    return level <= 3 && message && fault != 9;
}

// ====== initialize ======

static void initialize(void)
{
    unsigned index;
    unsigned sum = 0;
    boot.Info.PageTableRoot = 0x20000;
    window.Root = boot.Info.PageTableRoot;
    window.Pool = &pool;
    window.Active = 0;
    window.Ready = 1;
    pool.FreePages = 42;
    pool.PhysicalBits = 48;
    directory.Count = 1;
    directory.Entries[0] = 0x10000;
    reads = 0;
    fault = 0;
    for (index = 0; index < 56; ++index) { table[index] = 0; }
    table[0] = 'H'; table[1] = 'P'; table[2] = 'E'; table[3] = 'T';
    table[4] = 56; table[8] = 1; table[36] = 1;
    table[46] = 0xd0; table[47] = 0xfe;
    for (index = 0; index < 56; ++index) { sum += table[index]; }
    table[9] = (unsigned char)(0u - sum);
}

// ====== sentinel ======

static void sentinel(X86_HPET_DESCRIPTION *output)
{
    unsigned index;
    for (index = 0; index < sizeof(*output); ++index) {
        ((unsigned char *)output)[index] = 0xa5;
    }
}

// ====== unchanged ======

static int unchanged(const X86_HPET_DESCRIPTION *output)
{
    unsigned index;
    for (index = 0; index < sizeof(*output); ++index) {
        if (((const unsigned char *)output)[index] != 0xa5) { return 0; }
    }
    return 1;
}

// ====== run_hpet_kernel_tests ======

int run_hpet_kernel_tests(void)
{
    X86_HPET_DESCRIPTION output;
    static X86_PAGING_INVENTORY inventory;
    unsigned index;
    initialize(); sentinel(&output);
    if (!x86_hpet_kernel_capture(&boot, &window, &directory, &output) ||
        output.Physical != 0xfed00000 || reads != 2 || window.Active ||
        pool.FreePages != 42 || window.Root != 0x20000) { return 1; }
    initialize(); sentinel(&output); table[0] = 'F';
    if (!x86_hpet_kernel_capture(&boot, &window, &directory, &output) ||
        !unchanged(&output) || reads != 1) { return 2; }
    for (index = 1; index <= 9; ++index) {
        initialize(); sentinel(&output); fault = index;
        if (x86_hpet_kernel_capture(&boot, &window, &directory, &output) ||
            !unchanged(&output) || (index == 1 && reads)) { return (int)(10 + index); }
    }
    initialize(); sentinel(&output); window.Root = 1;
    if (x86_hpet_kernel_capture(&boot, &window, &directory, &output) ||
        reads || !unchanged(&output)) { return 20; }
    initialize(); sentinel(&output);
    if (x86_hpet_kernel_capture(NULL, &window, &directory, &output) ||
        x86_hpet_kernel_capture(&boot, NULL, &directory, &output) ||
        x86_hpet_kernel_capture(&boot, &window, NULL, &output) ||
        x86_hpet_kernel_capture(&boot, &window, &directory, NULL) ||
        reads || !unchanged(&output)) { return 21; }
    if (x86_hpet_kernel_capture(&boot, &window, &directory,
        (X86_HPET_DESCRIPTION *)(void *)&directory) || reads) { return 22; }
    initialize(); sentinel(&output); fault = 10;
    if (x86_hpet_kernel_initialize(&boot, &window, &directory, &inventory, &output) ||
        !unchanged(&output)) { return 23; }
    initialize(); sentinel(&output);
    if (!x86_hpet_kernel_initialize(&boot, &window, &directory, &inventory, &output) ||
        output.Physical != 0xfed00000) { return 24; }
    initialize();
    if (x86_hpet_kernel_initialize(&boot, &window, &directory, &inventory,
        (X86_HPET_DESCRIPTION *)(void *)&inventory) || reads) { return 25; }
    return 0;
}

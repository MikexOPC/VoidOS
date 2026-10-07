#include "../../probe/probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"

static KERNEL_BOOT_CONTEXT boot;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGE_WINDOW window;
static X86_PAGING_INVENTORY inventory;
static BOOT_U64 leaves[512];
static BOOT_U32 registers[2];
static unsigned fault, borrows, verifications;

// ====== x86_page_window_idle_valid ======

int x86_page_window_idle_valid(const X86_PAGE_WINDOW *owner)
{
    return owner == &window && !owner->Active && owner->Ready;
}

// ====== x86_paging_inventory_verify ======

int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *owner, const X86_PAGING_INVENTORY *snapshot)
{
    ++verifications;
    return context == &boot && owner == &pool && snapshot == &inventory &&
        fault != 1 && !(fault == 10 && verifications > 1);
}

// ====== kernel_inventory_memory ======

int kernel_inventory_memory(const unsigned char *map, BOOT_U64 bytes,
    BOOT_U64 stride, KERNEL_MEMORY_INVENTORY *output)
{
    (void)map; (void)bytes; (void)stride;
    output->Regions = 1;
    return 1;
}

// ====== kernel_read_region ======

int kernel_read_region(const unsigned char *map, BOOT_U64 bytes,
    BOOT_U64 stride, BOOT_U64 index, KERNEL_MEMORY_REGION *output)
{
    (void)map; (void)bytes; (void)stride; (void)index;
    output->Base = 0xfed00000; output->Pages = 1; output->Attributes = 1;
    output->Type = fault == 3 ? 7u : 11u;
    return 1;
}

// ====== kernel_regions_overlap ======

BOOT_U32 kernel_regions_overlap(BOOT_U64 first, BOOT_U64 pages,
    BOOT_U64 second, BOOT_U64 other_pages)
{
    return pages && other_pages && first < second + other_pages * 4096 &&
        second < first + pages * 4096;
}

// ====== x86_paging_view ======

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    return address == 0x8000 ? leaves : NULL;
}

// ====== x86_apic_cache_state ======

int x86_apic_cache_state(void) { return fault != 2; }

// ====== x86_apic_read_profile ======

int x86_apic_read_profile(X86_APIC_RAW *output)
{
    output->Pat = 0x0007040600070406ULL;
    output->Rflags = fault == 16 ? 0x202 : 2;
    output->PhysicalBits = fault == 17 ? 53u : 48u;
    return 1;
}

// ====== x86_apic_uc_slot ======

BOOT_U32 x86_apic_uc_slot(BOOT_U64 pat)
{
    (void)pat;
    return fault == 4 ? 0u : (fault == 18 ? 9u : 4u);
}

// ====== x86_apic_cache_bits ======

BOOT_U64 x86_apic_cache_bits(BOOT_U32 index) { return index == 3 ? 24 : 0; }

// ====== x86_window_bytes ======

volatile unsigned char *x86_window_bytes(void) { return (volatile unsigned char *)registers; }

// ====== x86_apic_borrow ======

int x86_apic_borrow(X86_PAGE_WINDOW *owner, BOOT_U64 physical, BOOT_U64 cache,
    int writable, int (*operation)(void *), void *state)
{
    int result;
    ++borrows;
    if (owner != &window || physical != 0xfed00000 || cache != 24 || writable) { return 0; }
    window.Active = 2;
    result = operation(state);
    window.Active = fault == 9 ? 2u : 0u;
    return result && fault != 8 && fault != 9;
}

// ====== kernel_section ======

int kernel_section(const char *name) { return name && fault != 15; }

// ====== kernel_text ======

int kernel_text(const char *text) { return text != NULL; }

// ====== kernel_number ======

int kernel_number(BOOT_U64 value) { (void)value; return 1; }

// ====== kernel_log ======

int kernel_log(unsigned int level, const char *message) { return level <= 3 && message; }

// ====== initialize ======

static void initialize(unsigned mode)
{
    unsigned index;
    fault = mode; borrows = 0; verifications = 0;
    window.Pool = &pool; window.Ready = 1; window.Active = 0;
    pool.PhysicalBits = 48; pool.FreePages = 42; pool.Count = 0;
    pool.Live[0].Id = 0;
    inventory.Count = 1; inventory.Tables[0].Physical = mode == 12 ? 0xfed00000 : 0x1000;
    inventory.Tables[0].View = 0x8000; inventory.Tables[0].Level = 3;
    for (index = 0; index < 512; ++index) { leaves[index] = 0; }
    if (mode == 11) { leaves[511] = 0xfed00003; }
    if (mode == 13) { pool.Live[0].Id = 1; pool.Live[0].Base = 0xfed00000; pool.Live[0].Pages = 1; }
    if (mode == 14) { pool.Count = 1; pool.Free[0].Base = 0xfed00000; pool.Free[0].Pages = 1; }
    registers[0] = mode == 5 ? 0x8086a202 : 0x8086a201;
    registers[1] = mode == 6 ? 0u : (mode == 7 ? 100000001u : 10000000u);
}

// ====== run_hpet_probe_tests ======

int run_hpet_probe_tests(void)
{
    X86_HPET_DESCRIPTION description = {0xfed00000, 0x8086a201, 0, 0, 0};
    unsigned mode;
    initialize(0);
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || borrows != 1 ||
        window.Active || pool.FreePages != 42 || verifications != 2) { return 1; }
    for (mode = 1; mode <= 18; ++mode) {
        initialize(mode);
        if (x86_hpet_probe(&boot, &window, &inventory, &description)) { return (int)(10 + mode); }
        if (mode != 9 && window.Active) { return 30; }
        if ((mode <= 4 || (mode >= 11 && mode <= 14) || mode >= 16) && borrows) { return 31; }
    }
    initialize(0); description.Physical += 8;
    if (x86_hpet_probe(&boot, &window, &inventory, &description) || borrows) { return 32; }
    initialize(0); description.Physical = UINT64_MAX - 4095;
    if (x86_hpet_probe(&boot, &window, &inventory, &description) || borrows) { return 33; }
    return 0;
}

#include "../../probe/probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#include <string.h>

static KERNEL_BOOT_CONTEXT boot;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGE_WINDOW window;
static X86_PAGING_INVENTORY inventory;
static BOOT_U64 leaves[512];
static BOOT_U32 registers[1024];
static BOOT_U32 original_registers[1024];
static unsigned fault, borrows, verifications;
static unsigned observed, unavailable, field;
static BOOT_U64 candidate, routes, fsb;

// ====== x86_paging_inventory_verify_lapic ======

int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *owner, const struct X86_LAPIC_MAPPING *mapping,
    const X86_PAGING_INVENTORY *snapshot)
{
    ++verifications;
    return context == &boot && owner == &pool && snapshot == &inventory &&
        mapping == (const struct X86_LAPIC_MAPPING *)&window && fault != 1;
}

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
    memcpy(original_registers, registers, sizeof(registers));
    result = operation(state);
    if (memcmp(original_registers, registers, sizeof(registers))) { result = 0; }
    window.Active = fault == 9 ? 2u : 0u;
    return result && fault != 8 && fault != 9;
}

// ====== kernel_section ======

int kernel_section(const char *name) { return name && fault != 15; }

// ====== kernel_text ======

int kernel_text(const char *text)
{
    if (!text) { return 0; }
    field = !strcmp(text, "KERNEL_HPET_CANDIDATE: ") ? 1u :
        (!strcmp(text, "\nKERNEL_HPET_ROUTE_MASK: ") ? 2u :
        (!strcmp(text, "\nKERNEL_HPET_FSB_CAPABLE: ") ? 3u : 0u));
    return 1;
}

// ====== kernel_number ======

int kernel_number(BOOT_U64 value)
{
    if (field == 1) { candidate = value; }
    if (field == 2) { routes = value; }
    if (field == 3) { fsb = value; }
    return 1;
}

// ====== kernel_log ======

int kernel_log(unsigned int level, const char *message)
{
    if (!message || level > 3) { return 0; }
    if (!strcmp(message, "KERNEL_HPET_CANDIDATE_OBSERVED")) { ++observed; }
    if (!strcmp(message, "KERNEL_HPET_NO_CANDIDATE")) { ++unavailable; }
    return 1;
}

// ====== initialize ======

static void initialize(unsigned mode)
{
    unsigned index;
    fault = mode; borrows = 0; verifications = 0;
    observed = 0; unavailable = 0; field = 0;
    candidate = 32; routes = 0; fsb = 0;
    window.Pool = &pool; window.Ready = 1; window.Active = 0;
    pool.PhysicalBits = 48; pool.FreePages = 42; pool.Count = 0;
    pool.Live[0].Id = 0;
    inventory.Count = 1; inventory.Tables[0].Physical = mode == 12 ? 0xfed00000 : 0x1000;
    inventory.Tables[0].View = 0x8000; inventory.Tables[0].Level = 3;
    for (index = 0; index < 512; ++index) { leaves[index] = 0; }
    if (mode == 11) { leaves[511] = 0xfed00003; }
    if (mode == 13) { pool.Live[0].Id = 1; pool.Live[0].Base = 0xfed00000; pool.Live[0].Pages = 1; }
    if (mode == 14) { pool.Count = 1; pool.Free[0].Base = 0xfed00000; pool.Free[0].Pages = 1; }
    for (index = 0; index < 1024; ++index) { registers[index] = 0; }
    registers[0] = mode == 5 ? 0x8086a202 : 0x8086a201;
    registers[1] = mode == 6 ? 0u : (mode == 7 ? 100000001u : 10000000u);
    for (index = 0; index < 3; ++index) {
        registers[(0x100 + 0x20 * index) / 4] = 32;
        registers[(0x104 + 0x20 * index) / 4] = 0x00f00000;
    }
    if (mode == 19) { registers[0x100 / 4] |= 1; }
    if (mode == 20) { registers[0x100 / 4] |= 8; }
    if (mode == 21) { registers[0] &= ~(1u << 15); registers[0x10 / 4] = 2; }
    if (mode == 22) { registers[0x20 / 4] = 8; }
}

// ====== run_hpet_probe_tests ======

int run_hpet_probe_tests(void)
{
    X86_HPET_DESCRIPTION description = {0xfed00000, 0x8086a201, 0, 0, 0};
    unsigned mode;
    initialize(0);
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || borrows != 1 ||
        window.Active || pool.FreePages != 42 || verifications != 2) { return 1; }
    if (observed != 1 || unavailable || candidate != 0 || routes != 0x00f00000 || fsb) { return 2; }
    for (mode = 1; mode <= 22; ++mode) {
        if (mode == 21) { continue; }
        initialize(mode);
        if (x86_hpet_probe(&boot, &window, &inventory, &description)) { return (int)(10 + mode); }
        if (mode != 9 && window.Active) { return 30; }
        if ((mode <= 4 || (mode >= 11 && mode <= 14) || (mode >= 16 && mode <= 18)) && borrows) {
            return 31;
        }
    }
    initialize(0); description.Physical += 8;
    if (x86_hpet_probe(&boot, &window, &inventory, &description) || borrows) { return 32; }
    initialize(0); description.Physical = UINT64_MAX - 4095;
    if (x86_hpet_probe(&boot, &window, &inventory, &description) || borrows) { return 33; }
    description.Physical = 0xfed00000;
    initialize(21); description.BlockId = registers[0];
    if (x86_hpet_probe(&boot, &window, &inventory, &description) || window.Active) { return 34; }
    description.BlockId = 0x8086a201;
    initialize(0); registers[0x10 / 4] = 2;
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || candidate != 2) { return 35; }
    initialize(0); registers[0x100 / 4] |= 4; registers[0x20 / 4] = 2;
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || candidate != 2) { return 36; }
    initialize(0);
    for (mode = 0; mode < 3; ++mode) { registers[(0x104 + 0x20 * mode) / 4] = 0; }
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || observed || unavailable != 1) {
        return 37;
    }
    registers[0x120 / 4] |= 32768;
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || candidate != 1 || routes || fsb != 1) {
        return 38;
    }
    initialize(0); registers[0] = description.BlockId = 0x8086bf01;
    for (mode = 0; mode < 32; ++mode) {
        registers[(0x100 + 0x20 * mode) / 4] = 32;
        registers[(0x104 + 0x20 * mode) / 4] = mode == 31 ? 0x80000000u : 0u;
    }
    if (!x86_hpet_probe(&boot, &window, &inventory, &description) || candidate != 31 ||
        routes != 0x80000000u || window.Active || pool.FreePages != 42) { return 39; }
    initialize(0);
    if (!x86_hpet_owned_preflight(&boot,&pool,&inventory,0xfed00000,
        (const struct X86_LAPIC_MAPPING *)&window) || verifications != 1 || borrows) { return 40; }
    for (mode = 1; mode <= 14; ++mode) {
        if (mode != 1 && mode != 3 && mode < 11) { continue; }
        if (mode == 10) { continue; }
        initialize(mode);
        if (x86_hpet_owned_preflight(&boot,&pool,&inventory,0xfed00000,
            (const struct X86_LAPIC_MAPPING *)&window) || borrows) { return (int)(40+mode); }
    }
    initialize(0);
    if (x86_hpet_owned_preflight(&boot,&pool,&inventory,0xfed00000,
        (const struct X86_LAPIC_MAPPING *)&pool)) { return 55; }
    return 0;
}

#include "mock.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include <string.h>
static KERNEL_BOOT_CONTEXT context;
static X86_PAGE_WINDOW window;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGING_INVENTORY inventory;
static X86_LAPIC_MAPPING mapping;
static KERNEL_MADT_TOPOLOGY topology;
static X86_APIC_PROFILE profile;
static X86_APIC_CAPABILITIES caps;
static X86_HPET_DESCRIPTION description;
static X86_IRQ_REGISTRY registry;
static X86_IRQ_CALLBACK handler, quiet_callback;
static BOOT_U64 borrowed;
static BOOT_U32 selected;
static BOOT_U32 selection_reads;
static BOOT_U32 stop_requests;
X86_WATCHDOG_ENV test_env;
BOOT_U32 test_mode, test_borrows, test_in_irq, test_interrupts;
BOOT_U32 test_hpet[1024], test_io[256];
__declspec(align(8)) volatile BOOT_U32 test_source[2];
volatile BOOT_U32 test_stop;

// ====== watchdog_test_reset ======

void watchdog_test_reset(BOOT_U32 mode)
{
    memset(x86_watchdog_state(),0,sizeof(X86_HPET_WATCHDOG));
    memset(test_hpet,0,sizeof(test_hpet)); memset(test_io,0,sizeof(test_io));
    memset(&topology,0,sizeof(topology)); memset(&caps,0,sizeof(caps));
    test_mode = mode; test_borrows = test_in_irq = test_interrupts = 0;
    handler = quiet_callback = NULL; borrowed = 0; selected = 7; selection_reads = 0;
    stop_requests = 0;
    test_source[0] = test_source[1] = test_stop = 0;
    pool.PhysicalBits = 48; pool.FreePages = 42;
    window.Pool = &pool; window.Root = 0x1000; window.Active = 0; window.Ready = 1;
    mapping.Root = 0x1000; mapping.Ready = 1;
    profile.Ready = 1; profile.InitialId = 0; profile.CacheBits = 24;
    profile.Raw.Pat = 0x0007040600070406ULL; profile.Raw.ApicBase = 0xfee00900;
    topology.IoCount = 1; topology.Io[0].Address = 0xfec00000;
    topology.Io[0].GsiBase = topology.Io[0].Id = 0;
    caps.Ready = 1; caps.IoCount = 1; caps.LapicId = 0;
    caps.Io[0].Address = 0xfec00000; caps.Io[0].Base = caps.Io[0].Id = 0;
    caps.Io[0].End = caps.Io[0].Inputs = 24; caps.Io[0].Version = 0x170020;
    description.Physical = 0xfed00000; description.BlockId = 0x8086a201;
    description.PageAttributes = 0;
    test_env.Context = &context; test_env.Window = &window; test_env.Inventory = &inventory;
    test_env.Mapping = &mapping; test_env.Topology = &topology;
    test_env.Profile = &profile; test_env.Capabilities = &caps; test_env.Description = &description;
    test_hpet[0] = description.BlockId; test_hpet[1] = 10000000;
    test_hpet[0xf0/4] = 1234; test_hpet[0xf4/4] = 4;
    for (unsigned index = 0; index < 3; ++index) {
        test_hpet[(0x100+index*32)/4] = 0x30;
        test_hpet[(0x104+index*32)/4] = 0xff0104;
        test_hpet[(0x108+index*32)/4] = 0x89abcdef;
        test_hpet[(0x10c+index*32)/4] = 0x12345678;
    }
    test_io[0] = 0; test_io[1] = 0x170020;
    for (unsigned index = 0; index < 24; ++index) { test_io[0x10+index*2] = 0x10040; }
    if (mode == 4) { test_hpet[0x10/4] = 1; }
    if (mode == 5) { test_hpet[0x100/4] |= 4; }
    if (mode == 6) { test_hpet[0x20/4] = 1; }
    if (mode == 7) { for (unsigned i=0; i<3; ++i) { test_hpet[(0x104+i*32)/4] = 0; } }
    if (mode == 8) { topology.IsoCount = 1; topology.Iso[0].Gsi = 16; }
    if (mode == 9) { for (unsigned i=16; i<24; ++i) { test_io[0x10+i*2] = 0x40; } }
    if (mode == 10) { caps.Io[0].Id = 1; }
    if (mode == 11) { test_hpet[0] ^= 1; }
    if (mode == 12) { test_hpet[1] = 0; }
    if (mode == 13) { caps.IoCount = 2; }
}

// ====== checked_environment_mocks ======

int x86_irq_ready(void) { return 1; }
// Legacy hardware fixtures do not allocate task leases; accounting has a separate test.
int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner) { return owner == &pool; }
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease)
{ (void)owner; (void)lease; return 0; }
int x86_apic_cache_state(void) { return 1; }
int x86_lapic_validate(const X86_LAPIC_MAPPING *owner) { return owner == &mapping; }
int x86_lapic_idle(const X86_LAPIC_MAPPING *owner)
{ return owner == &mapping && !test_in_irq && !(test_mode == 34 && test_interrupts); }
int x86_apic_read_profile(X86_APIC_RAW *cpu)
{
    cpu->Pat = profile.Raw.Pat; cpu->ApicBase = profile.Raw.ApicBase;
    cpu->PhysicalBits = 48; cpu->Rflags = 2; cpu->Reserved = 0; return 1;
}
int x86_apic_profile_valid(const KERNEL_MADT_TOPOLOGY *a, const X86_APIC_PROFILE *b, BOOT_U32 bits)
{ return a == &topology && b == &profile && bits == 48; }
BOOT_U32 x86_apic_io_inputs(BOOT_U32 version) { return version == 0x170020 ? 24u : 0u; }
int x86_hpet_owned_preflight(const KERNEL_BOOT_CONTEXT *a, const KERNEL_PHYSICAL_ALLOCATOR *b,
    const X86_PAGING_INVENTORY *c, BOOT_U64 physical, const struct X86_LAPIC_MAPPING *d)
{ return a == &context && b == &pool && c == &inventory && d == &mapping &&
    (physical == 0xfec00000 || physical == 0xfed00000); }
int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *a, const KERNEL_PHYSICAL_ALLOCATOR *b,
    const X86_LAPIC_MAPPING *c, const X86_PAGING_INVENTORY *d)
{ return a == &context && b == &pool && c == &mapping && d == &inventory; }
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *owner)
{ return owner == &window && !window.Active; }
int kernel_section(const char *text) { return text != NULL; }
int kernel_text(const char *text)
{
    if (text && test_mode == 27 && !strcmp(text,"KERNEL_WATCHDOG_FAILURE: ")) {
        test_mode = 0; return 0;
    }
    return text != NULL;
}
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(unsigned int level, const char *message) { return level <= 3 && message; }

// ====== borrow_without_allocation ======

int x86_apic_borrow(X86_PAGE_WINDOW *owner, BOOT_U64 physical, BOOT_U64 cache,
    int writable, int (*operation)(void *), void *argument)
{
    int result;
    if (owner != &window || window.Active || cache != 24 || writable < 0 || writable > 1) { return 0; }
    ++test_borrows; window.Active = 2; borrowed = physical;
    result = operation(argument);
    borrowed = 0; window.Active = 0;
    return result;
}

// ====== hpet_register_model ======

int x86_watchdog_mmio_read(BOOT_U32 offset, BOOT_U32 *value)
{
    if (borrowed != description.Physical || !value || (offset & 3) || offset >= 4096) { return 0; }
    if (test_mode == 25 && offset == 0x120 && ++selection_reads == 2) {
        test_mode = 0; return 0;
    }
    if (test_mode == 44 && offset == 0x20 && test_in_irq) {
        test_hpet[0xf0/4] = x86_watchdog_state()->Target;
        test_hpet[0x20/4] = 1u << x86_watchdog_state()->Timer;
        test_mode = 0;
    }
    if (offset == 0xf0 && ((test_hpet[0x10/4] & 1) ||
        (test_mode == 14 && x86_watchdog_state()->Armed))) {
        X86_HPET_WATCHDOG *owner = x86_watchdog_state();
        if (test_mode != 20) {
            test_hpet[offset/4] += test_mode == 14 && owner->Armed ? owner->Ticks * 3 / 4 : 1000u;
        }
    }
    *value = test_hpet[offset/4]; return 1;
}
int x86_watchdog_mmio_write(BOOT_U32 offset, BOOT_U32 value)
{
    if (borrowed != description.Physical || (offset & 3) || offset >= 4096) { return 0; }
    if (offset == 0x10 && !(value & 1)) ++stop_requests;
    if (offset == 0x10 && !(value & 1) &&
        ((test_mode == 15 && x86_watchdog_state()->Ready) ||
        (test_mode == 26 && stop_requests > 1))) { return 0; }
    if (test_mode == 21 && offset == 0x20) { return 0; }
    if (test_mode == 40 && offset == 0x10 && !(value & 1) && test_in_irq) return 0;
    if ((test_mode == 31 && offset == 0x10 && !(value & 1) && x86_watchdog_state()->Ready) ||
        (test_mode == 32 && offset == 0x10 && (value & 1) && x86_watchdog_state()->Armed) ||
        (test_mode == 33 && offset == 0x108)) {
        test_mode = 0;
        return 0;
    }
    if (test_mode == 29 && offset == 0x108 && (test_hpet[0x10/4] & 1)) {
        test_hpet[0xf0/4] += x86_watchdog_state()->Ticks * 3 / 4;
    }
    if (offset == 0x20) { test_hpet[offset/4] &= ~value; }
    else { test_hpet[offset/4] = value; }
    if (test_mode == 43 && offset == 0x20 && test_in_irq) test_mode = 44;
    if ((test_mode == 41 || test_mode == 42) && offset == 0x20 && test_in_irq) {
        test_hpet[0xf0/4] = x86_watchdog_state()->Target;
        if (test_mode == 42) test_hpet[0x20/4] = value;
        test_mode = 0;
    }
    if (test_mode == 30 && offset == 0x10 && (value & 1) && x86_watchdog_state()->Armed) {
        test_hpet[0xf0/4] = x86_watchdog_state()->Target;
        test_hpet[0x20/4] = 1u << x86_watchdog_state()->Timer;
    }
    return 1;
}

// ====== ioapic_register_model ======

int x86_apic_control_read(BOOT_U32 offset, BOOT_U32 *value)
{
    if (borrowed != 0xfec00000 || !value) { return 0; }
    if (offset == 0) { *value = selected; }
    else if (offset == 0x10) {
        if (test_mode == 24 && selected == 0x30) { test_mode = 0; return 0; }
        *value = test_io[selected];
    }
    else { return 0; }
    return 1;
}
int x86_apic_control_write(BOOT_U32 offset, BOOT_U32 value)
{
    if (borrowed != 0xfec00000) { return 0; }
    if (offset == 0 && value < 256) { selected = value; }
    else if (offset == 0x10) {
        if (test_mode == 18 && selected == 0x31) { test_mode = 0; return 0; }
        if (test_mode == 28 && selected == 0x30 && !x86_watchdog_state()->Acquired &&
            value == x86_watchdog_state()->IoLow) { test_io[selected] = value; return 0; }
        test_io[selected] = value;
        if (test_mode == 19 && selected == 0x30) { test_mode = 0; return 0; }
    }
    else { return 0; }
    return 1;
}

// ====== registry_and_eoi_contract ======

X86_IRQ_REGISTRY *x86_irq_registry(void) { return &registry; }
int x86_irq_registry_bind(X86_IRQ_REGISTRY *r, BOOT_U32 vector, X86_IRQ_CALLBACK h,
    X86_IRQ_CALLBACK q, X86_IRQ_TOKEN *token)
{
    if (r != &registry || vector != 0x40 || handler || !q() || test_mode == 16 || test_mode == 26) { return 0; }
    handler = h; quiet_callback = q; token->Id = 1; token->Vector = vector; token->Reserved = 0;
    return 1;
}
int x86_irq_registry_unbind(X86_IRQ_REGISTRY *r, X86_IRQ_TOKEN *token)
{
    if (r != &registry || !handler || !token->Id || !quiet_callback() || test_mode == 17) { return 0; }
    handler = quiet_callback = NULL; memset(token,0,sizeof(*token)); return 1;
}
int x86_irq_registry_token_live(const X86_IRQ_REGISTRY *r, const X86_IRQ_TOKEN *token)
{ return r == &registry && handler && token->Id == 1 && token->Vector == 0x40; }
int x86_lapic_eoi_vector(const X86_LAPIC_MAPPING *owner, BOOT_U32 vector)
{
    if (owner != &mapping || vector != 0x40 || !test_in_irq || test_hpet[0x20/4] || test_mode == 22) { return 0; }
    test_io[0x10+x86_watchdog_state()->Pin*2] &= ~0x4000u;
    test_in_irq = 0; return 1;
}

// ====== sleep_model_delivers_independent_irq ======

BOOT_U32 x86_irq_sleep(volatile BOOT_U32 *source, BOOT_U32 goal, BOOT_U32 budget)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    if (source != &owner->Count || goal != owner->Count + 1 || budget != 8 || !handler) { return 0; }
    if (test_mode != 2 && test_mode != 23) { ++test_source[0]; }
    if (test_mode == 3 || test_mode == 23) { test_stop = 1; }
    test_hpet[0xf0/4] = test_mode == 36 || test_mode == 38 ||
        (test_mode >= 41 && test_mode <= 43) ? 0 :
        test_mode == 37 ? owner->Start + 1 : owner->Target;
    test_hpet[0x20/4] = 1u << owner->Timer;
    if (test_mode == 39) test_hpet[0x20/4] |= 2;
    test_io[0x10+owner->Pin*2] |= 0x4000;
    test_in_irq = 1;
    if (!handler()) { return 0; }
    ++test_interrupts;
    if (test_mode == 36 || test_mode == 38) {
        if (!owner->Armed || owner->Count != goal - 1 || test_in_irq) return 0;
        test_hpet[0xf0/4] = test_mode == 38 ? 0 : owner->Target;
        test_hpet[0x20/4] = 1u << owner->Timer;
        test_io[0x10+owner->Pin*2] |= 0x4000;
        test_in_irq = 1;
        if (!handler()) return 0;
        ++test_interrupts;
    }
    if (test_mode == 35) test_hpet[0x20/4] = 1u << owner->Timer;
    return test_mode == 2 || test_mode == 23 ? 2u : 3u;
}

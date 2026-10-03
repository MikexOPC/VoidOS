#include "arch/x86/kernel/interrupts/apic/probe.h"
static KERNEL_BOOT_CONTEXT context;
static X86_PAGE_WINDOW window;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGING_INVENTORY ledger;
static KERNEL_MADT_TOPOLOGY topology;
static X86_APIC_PROFILE profile;
static X86_APIC_CAPABILITIES output;
static unsigned char map[80];
static BOOT_U64 entries[512];
static BOOT_U32 selector, invalidations, writes, reads, fault, order_error, cache_bad;
static BOOT_U32 lapic_id, lapic_version, io_id, io_version;

// ====== backend chiuso che distingue MMIO dalle normali letture RAM ===== //

int x86_apic_cache_state(void) { return !cache_bad; }
int x86_apic_read_profile(X86_APIC_RAW *raw) { *raw = profile.Raw; return 1; }
int kernel_section(const char *text) { return text != 0; }
int kernel_text(const char *text) { return text != 0; }
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(unsigned int level, const char *text) { (void)level; return text != 0; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address) { (void)address; return entries; }
volatile BOOT_U64 *x86_window_entries(BOOT_U32 level) { return level == 3 ? entries : 0; }
int x86_paging_inventory_verify(const KERNEL_BOOT_CONTEXT *ctx, const KERNEL_PHYSICAL_ALLOCATOR *owner,
                                const X86_PAGING_INVENTORY *inventory)
{
    return ctx == &context && owner == &pool && inventory == &ledger && ledger.Ready == 1 && !entries[32];
}
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *target)
{
    BOOT_U32 index;
    if (target != &window || window.Active || window.Lease.Id) return 0;
    for (index = 31; index <= 48; ++index) if (entries[index]) return 0;
    return 1;
}
void x86_invalidate_page(BOOT_U64 address)
{
    if (address != X86_KERNEL_PAGE_WINDOW || window.Active != 2 || pool.FreePages != 123) order_error = 1;
    ++invalidations;
    if (fault == 99 && invalidations == 1) entries[32] |= 4;
}
int x86_apic_mmio_read(BOOT_U32 offset, BOOT_U32 *value)
{
    BOOT_U64 physical = entries[32] & 0x000ffffffffff000ULL;
    ++reads;
    if (window.Active != 2 || !physical || (entries[32] & 0x98) != profile.CacheBits ||
        !(entries[32] & (1ULL << 63))) order_error = 1;
    if (fault && reads == fault) return 0;
    if (physical == topology.Lapic) {
        if (offset == 0x20) *value = lapic_id;
        else if (offset == 0x30) *value = lapic_version;
        else return 0;
    } else {
        if (offset == 0) *value = selector ^ ((fault == 100 && writes == 3) ? 1U : 0U);
        else if (offset == 0x10 && selector == 0) *value = io_id + (BOOT_U32)(((physical - 0xfec00000) / 4096) << 24);
        else if (offset == 0x10 && selector == 1) *value = io_version;
        else return 0;
    }
    entries[32] |= 0x20;
    return 1;
}
int x86_apic_mmio_select(BOOT_U32 value)
{
    if (window.Active != 2 || !(entries[32] & 2) || value > 255) order_error = 1;
    selector = value; ++writes; entries[32] |= 0x60;
    return 1;
}

// ====== fixture completa con RAM separata dai controller ===== //

static void clear(void *target, BOOT_U32 bytes)
{
    BOOT_U32 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)target)[index] = 0;
}
static void put(unsigned char *target, BOOT_U64 value)
{
    BOOT_U32 index;
    for (index = 0; index < 8; ++index) target[index] = (unsigned char)(value >> (8 * index));
}
static void fixture(void)
{
    clear(&context, sizeof(context)); clear(&pool, sizeof(pool)); clear(&window, sizeof(window));
    clear(&ledger, sizeof(ledger)); clear(&topology, sizeof(topology)); clear(&profile, sizeof(profile));
    clear(&output, sizeof(output)); clear(map, sizeof(map)); clear(entries, sizeof(entries));
    map[0] = 7; put(map + 8, 0x100000); put(map + 24, 128); map[32] = 8;
    context.MemoryMap = map; context.Info.MemoryMapBytes = 40; context.Info.MemoryMapCapacity = 80;
    context.Info.MemoryDescriptorBytes = 40;
    pool.PhysicalBits = 36; pool.FreePages = 123;
    window.Pool = &pool; ledger.Ready = 1; ledger.Count = 1;
    ledger.Tables[0].Physical = 0x1000; ledger.Tables[0].View = 0x400000;
    ledger.Tables[0].Level = 3;
    topology.Physical = 0x400000; topology.Lapic = 0xfee00000; topology.Length = 64;
    topology.Revision = 5; topology.Ready = 1; topology.CpuCount = topology.IoCount = 1;
    topology.Cpus[0].Uid = 99; topology.Cpus[0].Id = 7; topology.Cpus[0].Flags = 1;
    topology.Io[0].Address = 0xfec00000; topology.Io[0].Id = 2;
    profile.Raw.ApicBase = 0xfee00900; profile.Raw.Pat = 0x0007040600070406ULL;
    profile.Raw.Rflags = 2; profile.Raw.Leaf1Ebx = 7U << 24; profile.Raw.MaxLeaf = 1;
    profile.Raw.Leaf1Edx = 0x10220; profile.Raw.PhysicalBits = 36;
    {
        X86_APIC_RAW raw = profile.Raw;
        x86_apic_plan(&topology, &raw, 36, &profile);
    }
    lapic_id = 7U << 24; lapic_version = 0x00050014;
    io_id = 2U << 24; io_version = 0x00170020; selector = 17;
    invalidations = reads = writes = fault = order_error = cache_bad = 0;
}

// ====== successo e fallimenti preservano selettore budget e mapping vuoto ===== //

static int probe(void)
{
    return x86_apic_probe(&context, &window, &ledger, &topology, &profile, &output);
}
static int rejected(void)
{
    BOOT_U32 index;
    for (index = 0; index < sizeof(output); ++index) ((volatile unsigned char *)&output)[index] = 0xa5;
    if (probe()) return 0;
    for (index = 0; index < sizeof(output); ++index) if (((unsigned char *)&output)[index] != 0xa5) return 0;
    return !entries[32] && !window.Active && selector == 17 && pool.FreePages == 123 && !order_error;
}
static int lifecycle(void)
{
    BOOT_U32 failure;
    fixture();
    if (!probe() || output.Ready != 1 || output.LapicId != 7 || output.Io[0].Inputs != 24 ||
        output.Io[0].End != 24 || output.IoCount != 1 || invalidations != 4 ||
        writes != 3 || selector != 17 || entries[32] || window.Active || order_error) return 1;
    for (failure = 1; failure <= 6; ++failure) {
        fixture(); fault = failure;
        if (!rejected()) return 2;
    }
    fixture(); fault = 99; if (!rejected() || reads) return 9;
    fixture(); fault = 100; if (!rejected()) return 10;
    fixture(); lapic_id = 8U << 24; if (!rejected()) return 3;
    fixture(); lapic_version = 0xffffffff; if (!rejected()) return 4;
    fixture(); io_id = 3U << 24; if (!rejected()) return 5;
    fixture(); io_version = 0xffffffff; if (!rejected()) return 6;
    fixture(); cache_bad = 1; if (!rejected() || reads || invalidations) return 7;
    fixture(); ledger.Ready = 0; if (!rejected() || reads || invalidations) return 8;
    return 0;
}

// ====== piu IOAPIC limiti GSI e PAT UC nello slot quattro ===== //

static int extended(void)
{
    X86_APIC_RAW raw;
    fixture(); profile.Raw.Pat = 0x0606060006060606ULL; raw = profile.Raw;
    if (!x86_apic_plan(&topology, &raw, 36, &profile) || !probe() || profile.CacheBits != 0x80) return 1;
    fixture(); io_version = 0x00770020;
    if (!probe() || output.Io[0].Inputs != 120) return 2;
    fixture(); io_version = 0x00780020; if (!rejected()) return 3;
    fixture(); topology.Io[0].GsiBase = 0xfffffff0; if (!rejected()) return 4;
    fixture(); topology.Io[0].GsiBase = 0xffffffff; io_version = 0x20;
    if (!probe() || output.Io[0].End != (1ULL << 32)) return 5;
    fixture(); topology.IoCount = 2; topology.Length += 12;
    topology.Io[1].Address = 0xfec01000; topology.Io[1].Id = 3; topology.Io[1].GsiBase = 24;
    map[40] = 11; put(map + 48, 0xfec00000); put(map + 64, 2); map[72] = 1;
    context.Info.MemoryMapBytes = 80;
    if (!probe() || output.IoCount != 2 || output.Io[1].End != 48 || invalidations != 6 || selector != 17) return 6;
    topology.Io[1].GsiBase = 23; if (!rejected()) return 7;
    fixture(); map[40] = 11; put(map + 48, 0xfec00000); put(map + 64, 1); map[72] = 8;
    context.Info.MemoryMapBytes = 80; if (!rejected() || reads) return 8;
    fixture(); context.Info.MemoryMapCapacity = 1; if (!rejected() || reads) return 9;
    return 0;
}

// ====== controllo riserve firmware alias fisici e conflitti GSI ===== //

static int policies(void)
{
    BOOT_U32 type;
    for (type = 0; type <= 15; ++type) {
        fixture(); map[40] = (unsigned char)type; put(map + 48, 0xfec00000); put(map + 64, 1); map[72] = 1;
        context.Info.MemoryMapBytes = 80;
        if (type == 0 || type == 11) { if (!probe()) return 1; }
        else if (!rejected() || reads || invalidations) return 2;
    }
    fixture(); entries[40] = 0x123; if (probe() || reads || invalidations || entries[40] != 0x123) return 3;
    fixture(); entries[20] = 0xfec00003; if (!rejected() || reads || invalidations) return 4;
    fixture(); ledger.Tables[0].Physical = 0xfec00000; if (!rejected()) return 5;
    fixture(); topology.IsoCount = 1; topology.Length += 10; topology.Iso[0].Gsi = 24;
    if (!rejected()) return 6;
    fixture(); topology.NmiCount = 1; topology.Length += 8; topology.Nmi[0].Kind = 3; topology.Nmi[0].Value = 24;
    if (!rejected()) return 7;
    fixture(); map[40] = 11; put(map + 48, 0xfec00000); put(map + 64, 1); put(map + 72, 0x2001);
    context.Info.MemoryMapBytes = 80; if (!rejected() || reads) return 8;
    fixture();
    if (x86_apic_probe(&context, &window, &ledger, &topology, &profile, (X86_APIC_CAPABILITIES *)&topology) || reads) return 9;
    return 0;
}

// ====== suite MMIO con modello del selettore e delle PTE ===== //

__declspec(dllexport) int run_apic_probe_tests(void)
{
    int result = lifecycle(); if (result) return 100 + result;
    result = policies(); if (result) return 200 + result;
    result = extended(); if (result) return 300 + result;
    return 0;
}

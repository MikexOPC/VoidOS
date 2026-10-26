#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/window/access.h"
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGE_WINDOW window;
static BOOT_U64 tables[4][512];
static unsigned char bytes[16 * 4096], map[40];
static BOOT_U32 invalidations, ordering_error;
static BOOT_U64 initial_free;
static BOOT_U32 null_buffer;
static BOOT_U32 seen_pages, batch_calls;
static BOOT_U32 borrow_mode;

// ====== backend host senza istruzioni privilegiate ===== //

BOOT_U64 x86_kernel_root(void) { return 0x1000; }
volatile BOOT_U64 *x86_window_entries(BOOT_U32 level) { return level < 4 ? tables[level] : 0; }
volatile unsigned char *x86_window_bytes(void) { return null_buffer ? 0 : bytes; }
void x86_invalidate_page(BOOT_U64 address)
{
    BOOT_U32 index;
    BOOT_U64 offset = address - X86_KERNEL_PAGE_WINDOW;
    if (borrow_mode) {
        BOOT_U64 leaf = tables[3][32], base = leaf & 0x000ffffffffff000ULL;
        if (address != X86_KERNEL_PAGE_WINDOW || window.Active || window.Lease.Id ||
            pool.FreePages != initial_free || (leaf && leaf != (base | 1 | (1ULL << 63)))) ordering_error = 1;
        if (leaf) {
            for (index = 0; index < 4096; ++index) bytes[index] = (unsigned char)((base + index) % 251);
            tables[3][32] |= 0x20;
        }
        ++invalidations;
        return;
    }
    if (address < X86_KERNEL_PAGE_WINDOW || address >= X86_KERNEL_PAGE_WINDOW + window.Lease.Pages * 4096 ||
        (address & 4095) || pool.FreePages != initial_free - window.Lease.Pages ||
        !kernel_physical_is_pinned(&pool, &window.Lease)) ordering_error = 1;
    if (offset < 16 * 4096 && !(offset & 4095)) {
        BOOT_U32 bit = 1U << (BOOT_U32)(offset / 4096);
        if (seen_pages & bit) ordering_error = 1;
        seen_pages |= bit;
    }
    for (index = 0; index < window.Lease.Pages; ++index)
        if ((!tables[3][32 + index]) != (window.Active != 0)) ordering_error = 1;
    ++invalidations;
    if (++batch_calls == window.Lease.Pages) {
        if (seen_pages != (1U << (BOOT_U32)window.Lease.Pages) - 1) ordering_error = 1;
        batch_calls = seen_pages = 0;
    }
}

// ====== mappa firmware e tabelle indipendenti per ogni scenario ===== //

static void clear(void *object, BOOT_U64 size)
{
    BOOT_U64 index;
    for (index = 0; index < size; ++index) ((volatile unsigned char *)object)[index] = 0;
}

static int fixture(void)
{
    BOOT_U32 index;
    clear(&pool, sizeof(pool)); clear(&window, sizeof(window)); clear(tables, sizeof(tables)); clear(map, sizeof(map));
    map[0] = 7; map[9] = 0x10; map[24] = 32; map[32] = 8;
    tables[0][X86_BOOT_RECURSIVE_SLOT] = 0x1003 | (1ULL << 63);
    tables[0][(X86_KERNEL_PAGE_WINDOW >> 39) & 511] = 0x2003;
    tables[1][0] = 0x3003; tables[2][0] = 0x4003;
    for (index = 0; index < sizeof(bytes); ++index) ((volatile unsigned char *)bytes)[index] = 0xa5;
    invalidations = ordering_error = null_buffer = borrow_mode = 0;
    batch_calls = seen_pages = 0;
    if (!kernel_physical_initialize(&pool, map, sizeof(map), sizeof(map), 32)) return 0;
    initial_free = pool.FreePages;
    return 1;
}

// ====== ordine TLB ownership handle alterato e azzeramento dopo riuso ===== //

static int repeated_sizes(void)
{
    BOOT_U32 cycle, index;
    BOOT_U64 previous_id = 0;
    KERNEL_PHYSICAL_LEASE stale;
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000)) return 80;
    tables[3][1] = 0x800003 | (1ULL << 63);
    for (cycle = 0; cycle < 256; ++cycle) {
        BOOT_U64 pages = 1 + ((cycle * 7) % 16);
        BOOT_U32 before = invalidations;
        if (!x86_page_window_acquire(&window, pages) || window.Lease.Id <= previous_id ||
            pool.FreePages != initial_free - pages) return 81;
        previous_id = window.Lease.Id;
        stale = window.Lease;
        for (index = 0; index < pages * 4096; ++index) {
            if (bytes[index]) return 82;
            bytes[index] = (unsigned char)(cycle + index);
        }
        if (!x86_page_window_release(&window) || kernel_physical_release(&pool, &stale) ||
            ordering_error || invalidations != before + 2 * pages || pool.FreePages != initial_free ||
            !kernel_physical_validate(&pool) || tables[3][1] != (0x800003 | (1ULL << 63))) return 83;
        for (index = 31; index <= 48; ++index) {
            if (tables[3][index]) return 84;
        }
    }
    return 0;
}

// ====== preflight deve rifiutare prima di pubblicare qualsiasi foglia ===== //

static int rejects_every_occupied_slot(void)
{
    BOOT_U32 slot;
    for (slot = 31; slot <= 48; ++slot) {
        if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000)) return 85;
        tables[3][slot] = 0x9003;
        if (x86_page_window_acquire(&window, 16) || invalidations || window.Lease.Id ||
            window.Active || pool.FreePages != initial_free || pool.NextId != 1) return 86;
        if (tables[3][slot] != 0x9003) return 87;
    }
    return 0;
}

// ====== letture ACPI tra pagine con budget e ordine delle invalidazioni verificati ===== //

static int borrowed_reads(void)
{
    KERNEL_BOOT_CONTEXT context;
    unsigned char firmware_map[40];
    static unsigned char output[8192];
    BOOT_U32 index;
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000)) return 90;
    clear(&context, sizeof(context)); clear(firmware_map, sizeof(firmware_map));
    context.Info.Signature = BOOT_START_SIGNATURE; context.Info.Version = BOOT_START_VERSION;
    context.Info.Size = sizeof(context.Info); context.Info.RequiredFeatures = context.Info.EnabledFeatures = 3;
    context.Info.PageTableRoot = 0x1000; context.Info.KernelEntry = X86_KERNEL_IMAGE_BASE + 0x1000ULL;
    context.Info.StackBase = X86_BOOT_STACK_VIRTUAL; context.Info.StackBytes = X86_BOOT_STACK_BYTES;
    context.Info.MemoryMapVirtual = X86_BOOT_MAP_VIRTUAL; context.Info.MemoryMapBytes = 40;
    context.Info.MemoryMapCapacity = 4096; context.Info.MemoryDescriptorBytes = 40; context.Info.MemoryDescriptorVersion = 1;
    context.MemoryMap = firmware_map;
    firmware_map[0] = 9; firmware_map[10] = 0x80; firmware_map[24] = 3; firmware_map[32] = 8;
    borrow_mode = 1;
    if (!x86_page_window_read_acpi(&window, &context, 0x800fff, 8192, output) ||
        invalidations != 6 || ordering_error || tables[3][32] || window.Active || window.Lease.Id ||
        pool.FreePages != initial_free || pool.NextId != 1) return 91;
    for (index = 0; index < sizeof(output); ++index)
        if (output[index] != (unsigned char)((0x800fffULL + index) % 251)) return 92;
    for (index = 0; index < 15; ++index) {
        firmware_map[0] = (unsigned char)index;
        if (index == 9 || index == 10) continue;
        if (x86_page_window_read_acpi(&window, &context, 0x800000, 36, output) || invalidations != 6) return 93;
    }
    firmware_map[0] = 10;
    if (!x86_page_window_read_acpi(&window, &context, 0x800007, 36, output) || invalidations != 8) return 94;
    firmware_map[33] = 0x20;
    if (x86_page_window_read_acpi(&window, &context, 0x800000, 36, output)) return 95;
    firmware_map[33] = 0;
    if (x86_page_window_read_acpi(&window, &context, 0x802fff, 2, output) ||
        x86_page_window_read_acpi(&window, &context, ~0ULL, 36, output) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 0, output) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 65537, output) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 36, firmware_map) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 36, (unsigned char *)&pool) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 36, (unsigned char *)&window) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 36, (unsigned char *)&context) ||
        x86_page_window_read_acpi(&window, &context, 0x800000, 36, (unsigned char *)X86_KERNEL_PAGE_WINDOW)) return 96;
    null_buffer = 1;
    if (x86_page_window_read_acpi(&window, &context, 0x800000, 36, output) || invalidations != 8) return 97;
    null_buffer = 0; tables[3][47] = 0x9003;
    if (x86_page_window_read_acpi(&window, &context, 0x800000, 36, output) || invalidations != 8) return 98;
    tables[3][47] = 0; borrow_mode = 0;
    if (!x86_page_window_acquire(&window, 1)) return 99;
    if (x86_page_window_read_acpi(&window, &context, 0x800000, 36, output)) return 100;
    if (!x86_page_window_release(&window) || ordering_error) return 101;
    return 0;
}

// ====== parcheggio e ripresa con contenuto conservato ===== //

static int parked_cycle(void)
{
    KERNEL_PHYSICAL_LEASE parked, held;
    BOOT_U64 base;

    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000) || !x86_page_window_acquire(&window, 3)) return 110;

    clear(&parked, sizeof(parked));
    base = window.Lease.Base;
    bytes[0] = 0x11; bytes[3 * 4096 - 1] = 0x22;

    if (x86_page_window_resume(&window, &parked)) return 111;

    parked.Pages = 1;
    if (x86_page_window_park(&window, &parked) || !window.Active) return 112;

    parked.Pages = 0;
    if (x86_page_window_park(&window, (KERNEL_PHYSICAL_LEASE *)&window.Lease) || !window.Active) return 113;

    if (!x86_page_window_park(&window, &parked) || window.Active || window.Lease.Id || tables[3][32] || tables[3][34] ||
        parked.Base != base || parked.Pages != 3 || !kernel_physical_is_pinned(&pool, &parked) ||
        pool.FreePages != initial_free - 3 || !x86_page_window_idle_valid(&window) || ordering_error) return 114;

    held = parked;
    if (x86_page_window_park(&window, &held) || x86_page_window_release(&window)) return 115;

    held.Pages = 17;
    if (x86_page_window_resume(&window, &held)) return 116;

    held = parked; ++held.Id;
    if (x86_page_window_resume(&window, &held) || window.Active) return 117;

    if (!kernel_physical_pin(&pool, &parked, 0)) return 118;
    if (x86_page_window_resume(&window, &parked) || window.Active) return 119;
    if (!kernel_physical_pin(&pool, &parked, 1)) return 120;

    // The parked span keeps its bytes: resume never zeroes, unlike acquire.
    if (!x86_page_window_resume(&window, &parked) || !window.Active || window.Lease.Base != base ||
        parked.Base || parked.Pages || parked.Id || bytes[0] != 0x11 || bytes[3 * 4096 - 1] != 0x22 ||
        tables[3][32] != (base | 3 | (1ULL << 63)) || tables[3][34] != ((base + 8192) | 3 | (1ULL << 63))) return 121;

    if (x86_page_window_resume(&window, &held) || x86_page_window_acquire(&window, 1)) return 122;

    if (!x86_page_window_park(&window, &parked) || !x86_page_window_resume(&window, &parked) ||
        !x86_page_window_release(&window) || pool.FreePages != initial_free || ordering_error) return 123;

    return 0;
}

// ====== suite pagina posseduta e prestito ACPI senza trasferimenti ownership ===== //

__declspec(dllexport) int run_window_tests(void)
{
    BOOT_U32 index, scenario;
    BOOT_U64 id, entry;
    X86_PAGE_WINDOW copy;
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000) ||
        !x86_page_window_acquire(&window, 1) || pool.FreePages != initial_free - 1 || invalidations != 1) return 1;
    for (index = 0; index < 4096; ++index) if (bytes[index]) return 2;
    if (kernel_physical_release(&pool, &window.Lease) || x86_page_window_acquire(&window, 1)) return 3;
    copy = window;
    if (x86_page_window_release(&copy)) return 4;
    id = window.Lease.Id; entry = tables[3][32]; ++window.Lease.Id;
    if (x86_page_window_release(&window) || tables[3][32] != entry || invalidations != 1) return 5;
    window.Lease.Id = id;
    tables[3][32] |= 4;
    if (x86_page_window_release(&window) || invalidations != 1 || !kernel_physical_is_pinned(&pool, &window.Lease)) return 32;
    tables[3][32] = entry;
    tables[1][0] |= 0x80;
    if (x86_page_window_release(&window) || tables[3][32] != entry || invalidations != 1) return 33;
    tables[1][0] = 0x3003;
    tables[3][32] |= 0x60;
    bytes[0] = 0x5a; bytes[4095] = 0xa5;
    if (!x86_page_window_release(&window) || tables[3][32] || pool.FreePages != initial_free ||
        invalidations != 2 || ordering_error || x86_page_window_release(&window)) return 6;
    if (!x86_page_window_acquire(&window, 1) || window.Lease.Id == id) return 7;
    for (index = 0; index < 4096; ++index) if (bytes[index]) return 8;
    if (!x86_page_window_release(&window) || ordering_error || !kernel_physical_validate(&pool)) return 9;
    for (scenario = 0; scenario < 7; ++scenario) {
        if (!fixture()) return 10;
        switch (scenario) {
        case 0: tables[0][X86_BOOT_RECURSIVE_SLOT] = 0; break;
        case 1: tables[0][X86_BOOT_RECURSIVE_SLOT] &= ~(1ULL << 63); break;
        case 2: tables[1][0] |= 0x80; break;
        case 3: tables[2][0] |= 4; break;
        case 4: tables[3][31] = 0x5003; break;
        case 5: tables[3][48] = 0x5003; break;
        default: tables[3][32] = 0x5003; break;
        }
        if (x86_page_window_initialize(&window, &pool, 0x1000) || window.Ready || invalidations ||
            pool.FreePages != initial_free) return 20 + (int)scenario;
    }
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000)) return 30;
    {
        KERNEL_PHYSICAL_LEASE all;
        clear(&all, sizeof(all));
        if (!kernel_physical_allocate(&pool, initial_free, &all) || x86_page_window_acquire(&window, 1) ||
            tables[3][32] || invalidations || !kernel_physical_release(&pool, &all)) return 31;
    }
    for (scenario = 1; scenario <= 16; ++scenario) {
        BOOT_U64 base;
        if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000) ||
            !x86_page_window_acquire(&window, scenario)) return 40 + (int)scenario;
        base = window.Lease.Base;
        if (pool.FreePages != initial_free - scenario || invalidations != scenario || ordering_error) return 60;
        for (index = 0; index < scenario; ++index) {
            if (tables[3][32 + index] != ((base + index * 4096) | 3 | (1ULL << 63))) return 61;
            if (bytes[index * 4096] || bytes[index * 4096 + 4095]) return 62;
            bytes[index * 4096] = (unsigned char)index + 1;
            bytes[index * 4096 + 4095] = 0x5a;
        }
        for (index = scenario * 4096; index < sizeof(bytes); ++index) {
            if (bytes[index] != 0xa5) return 74;
        }
        tables[3][31 + scenario] |= 4;
        if (x86_page_window_release(&window) || invalidations != scenario || !tables[3][32] ||
            !kernel_physical_is_pinned(&pool, &window.Lease)) return 63;
        tables[3][31 + scenario] &= ~4ULL;
        if (!x86_page_window_release(&window) || invalidations != 2 * scenario || ordering_error ||
            pool.FreePages != initial_free || window.Lease.Id) return 64;
        for (index = 31; index <= 48; ++index) if (tables[3][index]) return 65;
        if (!x86_page_window_acquire(&window, scenario)) return 66;
        for (index = 0; index < scenario * 4096; ++index) if (bytes[index]) return 67;
        if (!x86_page_window_release(&window) || ordering_error || pool.FreePages != initial_free) return 68;
    }
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000)) return 69;
    if (x86_page_window_acquire(&window, 0) || x86_page_window_acquire(&window, 17) ||
        x86_page_window_acquire(&window, ~0ULL) || pool.FreePages != initial_free || pool.NextId != 1 ||
        window.Active || invalidations) return 70;
    tables[3][47] = 0x9003;
    if (x86_page_window_acquire(&window, 1) || pool.FreePages != initial_free || pool.NextId != 1) return 71;
    tables[3][47] = 0;
    null_buffer = 1;
    if (x86_page_window_acquire(&window, 16) || pool.FreePages != initial_free || invalidations) return 72;
    null_buffer = 0;
    {
        KERNEL_PHYSICAL_LEASE occupied;
        clear(&occupied, sizeof(occupied));
        if (!kernel_physical_allocate(&pool, 17, &occupied) || x86_page_window_acquire(&window, 16) ||
            invalidations || tables[3][32] || window.Active || window.Lease.Id ||
            !kernel_physical_release(&pool, &occupied)) return 73;
    }
    {
        int result = repeated_sizes();
        if (result) return result;
    }
    {
        int result = rejects_every_occupied_slot();
        if (result) return result;
    }
    {
        int result = borrowed_reads();
        if (result) return result;
    }
    return parked_cycle();
}

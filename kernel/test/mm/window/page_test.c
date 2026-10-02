#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/window/access.h"
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGE_WINDOW window;
static BOOT_U64 tables[4][512];
static unsigned char bytes[4096], map[40];
static BOOT_U32 invalidations, ordering_error;
static BOOT_U64 initial_free;

// ====== backend host senza istruzioni privilegiate ====== //

BOOT_U64 x86_kernel_root(void) { return 0x1000; }
volatile BOOT_U64 *x86_window_entries(BOOT_U32 level) { return level < 4 ? tables[level] : 0; }
volatile unsigned char *x86_window_bytes(void) { return bytes; }
void x86_invalidate_page(BOOT_U64 address)
{
    if (address != X86_KERNEL_PAGE_WINDOW || pool.FreePages != initial_free - 1 ||
        !kernel_physical_is_pinned(&pool, &window.Lease)) ordering_error = 1;
    if ((!tables[3][32]) != ((invalidations & 1) != 0)) ordering_error = 1;
    ++invalidations;
}

// ====== mappa firmware e tabelle indipendenti per ogni scenario ====== //

static void clear(void *object, BOOT_U64 size)
{
    BOOT_U64 index;
    for (index = 0; index < size; ++index) ((volatile unsigned char *)object)[index] = 0;
}

static int fixture(void)
{
    BOOT_U32 index;
    clear(&pool, sizeof(pool)); clear(&window, sizeof(window)); clear(tables, sizeof(tables)); clear(map, sizeof(map));
    map[0] = 7; map[9] = 0x10; map[24] = 4; map[32] = 8;
    tables[0][X86_BOOT_RECURSIVE_SLOT] = 0x1003 | (1ULL << 63);
    tables[0][(X86_KERNEL_PAGE_WINDOW >> 39) & 511] = 0x2003;
    tables[1][0] = 0x3003; tables[2][0] = 0x4003;
    for (index = 0; index < 4096; ++index) ((volatile unsigned char *)bytes)[index] = 0xa5;
    invalidations = ordering_error = 0;
    if (!kernel_physical_initialize(&pool, map, sizeof(map), sizeof(map), 32)) return 0;
    initial_free = pool.FreePages;
    return 1;
}

// ====== ordine TLB ownership handle alterato e azzeramento dopo riuso ====== //

__declspec(dllexport) int run_window_tests(void)
{
    BOOT_U32 index, scenario;
    BOOT_U64 id, entry;
    X86_PAGE_WINDOW copy;
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000) ||
        !x86_page_window_acquire(&window) || pool.FreePages != initial_free - 1 || invalidations != 1) return 1;
    for (index = 0; index < 4096; ++index) if (bytes[index]) return 2;
    if (kernel_physical_release(&pool, &window.Lease) || x86_page_window_acquire(&window)) return 3;
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
    if (!x86_page_window_acquire(&window) || window.Lease.Id == id) return 7;
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
        case 5: tables[3][33] = 0x5003; break;
        default: tables[3][32] = 0x5003; break;
        }
        if (x86_page_window_initialize(&window, &pool, 0x1000) || window.Ready || invalidations ||
            pool.FreePages != initial_free) return 20 + (int)scenario;
    }
    if (!fixture() || !x86_page_window_initialize(&window, &pool, 0x1000)) return 30;
    {
        KERNEL_PHYSICAL_LEASE all;
        clear(&all, sizeof(all));
        if (!kernel_physical_allocate(&pool, initial_free, &all) || x86_page_window_acquire(&window) ||
            tables[3][32] || invalidations || !kernel_physical_release(&pool, &all)) return 31;
    }
    return 0;
}

#include "arch/x86/mm/paging/inventory.h"
static BOOT_U64 tables[129][512], views[129];
static BOOT_U32 table_count, reads, missing;
static unsigned char map[96];
static KERNEL_BOOT_CONTEXT context;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGING_INVENTORY ledger, saved;

// ====== backend indipendente che conta ogni pagina realmente consultata ===== //

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    BOOT_U32 index;
    ++reads;
    if (missing) return 0;
    for (index = 0; index < table_count; ++index)
        if (views[index] == address) return tables[index];
    return 0;
}

// ====== azzeramento esplicito senza dipendenze dal runtime host ===== //

static void clear(void *object, BOOT_U64 bytes)
{
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)object)[index] = 0;
}

// ====== descrittori little endian con riserva loader separata dalla RAM libera ===== //

static void number(unsigned char *buffer, BOOT_U64 value)
{
    BOOT_U32 index;
    for (index = 0; index < 8; ++index) buffer[index] = (unsigned char)(value >> (8 * index));
}

// ====== copia bounded per preservare la fotografia senza memcpy implicita ===== //

static void save_ledger(void)
{
    BOOT_U32 index;
    for (index = 0; index < sizeof(ledger); ++index)
        ((volatile unsigned char *)&saved)[index] = ((const unsigned char *)&ledger)[index];
}

// ====== grafo di quattro livelli piu un ramo distinto nello stesso PD ===== //

static int fixture(void)
{
    BOOT_U32 level;
    clear(tables, sizeof(tables)); clear(views, sizeof(views)); clear(map, sizeof(map));
    clear(&context, sizeof(context)); clear(&pool, sizeof(pool)); clear(&ledger, sizeof(ledger));
    table_count = 5; reads = missing = 0;
    map[0] = 2; number(map + 8, 4096); number(map + 24, 129); map[32] = 8;
    map[48] = 7; number(map + 56, 0x100000); number(map + 72, 128); map[80] = 8;
    context.MemoryMap = map; context.Info.MemoryMapBytes = 96;
    context.Info.MemoryDescriptorBytes = 48; context.Info.PageTableRoot = 4096;
    views[0] = (128ULL << 39) | (128ULL << 30) | (128ULL << 21) | (128ULL << 12);
    for (level = 0; level < 3; ++level) {
        BOOT_U32 slot = level ? 0 : 224;
        tables[level][slot] = ((BOOT_U64)level + 2) * 4096 | 3;
        views[level + 1] = x86_paging_child_view(views[level], level, slot);
    }
    tables[0][128] = 0x8000000000001003ULL;
    tables[2][1] = 0x5003;
    views[4] = x86_paging_child_view(views[2], 2, 1);
    tables[3][1] = 0x8000000000100003ULL;
    tables[4][511] = 0x200001;
    return kernel_physical_initialize(&pool, map, 96, 48, 32);
}

// ====== pubblicazione atomica per errori e fotografia verificabile ===== //

static int lifecycle(void)
{
    BOOT_U32 index;
    if (!fixture() || !x86_paging_inventory_capture(&context, &pool, &ledger) ||
        ledger.Count != 5 || ledger.LeafPages != 2 || ledger.Root != 4096 || ledger.Ready != 1 ||
        !x86_paging_inventory_verify(&context, &pool, &ledger) || pool.FreePages != 128) return 1;
    for (index = 0; index < 5; ++index)
        if (ledger.Tables[index].Physical != ((BOOT_U64)index + 1) * 4096 ||
            ledger.Tables[index].View != views[index] || ledger.Tables[index].Reserved) return 2;
    save_ledger();
    tables[3][1] |= 0x60;
    if (!x86_paging_inventory_verify(&context, &pool, &ledger)) return 3;
    tables[3][2] = 0x300001;
    if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 4;
    tables[3][2] = 0; missing = 1;
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 5;
    for (index = 0; index < sizeof(ledger); ++index)
        if (((unsigned char *)&ledger)[index] != ((unsigned char *)&saved)[index]) return 6;
    return 0;
}

// ====== nessun accesso al figlio se indirizzo o flags del genitore sono invalidi ===== //

static int corruption(void)
{
    BOOT_U32 bit, level;
    for (level = 0; level < 3; ++level) for (bit = 0; bit < 64; ++bit) {
        BOOT_U32 slot = level ? 0 : 224;
        BOOT_U64 original;
        if (!fixture()) return 1;
        if (!x86_paging_inventory_capture(&context, &pool, &ledger)) return 7;
        original = tables[level][slot];
        tables[level][slot] ^= 1ULL << bit;
        if (bit == 5 || bit == 6) {
            if (!x86_paging_inventory_capture(&context, &pool, &ledger)) return 2;
        } else if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 3;
        tables[level][slot] = original;
    }
    if (!fixture()) return 4;
    tables[0][224] = 0x1003;
    if (x86_paging_inventory_capture(&context, &pool, &ledger) || reads != 1) return 5;
    tables[0][224] = 0x2003; tables[2][1] = 0x4003; reads = 0;
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 6;
    if (!fixture()) return 7;
    tables[0][256] = 0x2003;
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 8;
    if (!fixture()) return 9;
    tables[3][1] = 0x8000000000005003ULL;
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 10;
    return 0;
}

// ====== risorse paging mai libere o possedute dall'allocatore fisico ===== //

static int reservations(void)
{
    BOOT_U32 index;
    for (index = 0; index < 5; ++index) {
        if (!fixture()) return 1;
        tables[0][224] = 0x100003;
        if (x86_paging_inventory_capture(&context, &pool, &ledger) || reads != 1) return 2;
        if (!fixture()) return 3;
        map[0] = (unsigned char)(index == 0 ? 7 : index);
        if (index == 2) map[32] = 1;
        if (x86_paging_inventory_capture(&context, &pool, &ledger) || reads) return 4;
    }
    if (!fixture()) return 5;
    pool.Free[0].Base = 4096;
    if (x86_paging_inventory_capture(&context, &pool, &ledger) || reads) return 6;
    return 0;
}

// ====== limite esatto di 128 tabelle e rifiuto della centoventinovesima ===== //

static int capacity(void)
{
    BOOT_U32 index;
    if (!fixture()) return 1;
    clear(tables, sizeof(tables)); clear(views, sizeof(views));
    views[0] = (128ULL << 39) | (128ULL << 30) | (128ULL << 21) | (128ULL << 12);
    tables[0][128] = 0x8000000000001003ULL;
    table_count = 129;
    for (index = 1; index < 129; ++index) {
        tables[0][index - 1] = ((BOOT_U64)index + 1) * 4096 | 3;
        views[index] = x86_paging_child_view(views[0], 0, index - 1);
    }
    tables[0][127] = 0;
    if (!x86_paging_inventory_capture(&context, &pool, &ledger) || ledger.Count != 128) return 2;
    save_ledger(); tables[0][127] = 129 * 4096 | 3;
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 3;
    for (index = 0; index < sizeof(ledger); ++index)
        if (((unsigned char *)&ledger)[index] != ((unsigned char *)&saved)[index]) return 4;
    return 0;
}

// ====== alias di output e metadati alterati prima di consultare il grafo ===== //

static int metadata(void)
{
    BOOT_U32 index;
    if (!fixture() || !x86_paging_inventory_capture(&context, &pool, &ledger)) return 1;
    if (x86_paging_inventory_capture(0, &pool, &ledger) ||
        x86_paging_inventory_capture(&context, 0, &ledger) ||
        x86_paging_inventory_capture(&context, &pool, 0)) return 4;
    if (x86_paging_inventory_capture(&context, &pool, (X86_PAGING_INVENTORY *)&pool) ||
        x86_paging_inventory_capture(&context, &pool, (X86_PAGING_INVENTORY *)map) ||
        x86_paging_inventory_capture(&context, &pool, (X86_PAGING_INVENTORY *)&context)) return 2;
    for (index = 0; index < sizeof(ledger) * 8; ++index) {
        unsigned char *bytes = (unsigned char *)&ledger;
        bytes[index / 8] ^= (unsigned char)(1U << (index % 8));
        if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 3;
        bytes[index / 8] ^= (unsigned char)(1U << (index % 8));
    }
    return 0;
}

// ====== esecuzione di gruppi con codici di errore identificabili ===== //

__declspec(dllexport) int run_paging_inventory_tests(void)
{
    int result = lifecycle();
    if (result) return 100 + result;
    result = corruption(); if (result) return 200 + result;
    result = reservations(); if (result) return 300 + result;
    result = capacity(); if (result) return 400 + result;
    result = metadata(); if (result) return 500 + result;
    return 0;
}

#include "fixture.h"

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
        BOOT_U32 slot = level ? 0 : X86_LAYOUT_WINDOWS_SLOT;
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
    tables[0][X86_LAYOUT_WINDOWS_SLOT] = 0x1003;
    if (x86_paging_inventory_capture(&context, &pool, &ledger) || reads != 1) return 5;
    tables[0][X86_LAYOUT_WINDOWS_SLOT] = 0x2003; tables[2][1] = 0x4003; reads = 0;
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
        tables[0][X86_LAYOUT_WINDOWS_SLOT] = 0x100003;
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
    views[0] = X86_LAYOUT_RECURSIVE_VIEW(X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT);
    tables[0][X86_LAYOUT_RECURSIVE_SLOT] = 0x8000000000001003ULL;
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

// ====== inventario misto con MMIO distinto e nessuna PTE UC implicita ===== //

static int mixed(void)
{
    BOOT_U32 bit;
    if (!fixture()) return 1;
    mapping.Self = (BOOT_U64)&mapping; mapping.Ready = 1; mapping.Root = 4096;
    mapping.Physical = 0xfee00000; mapping.Cache = 0x18; mapping.PhysicalBits = 32;
    tables[3][288] = 0x80000000fee0001bULL;
    if (x86_paging_inventory_capture(&context,&pool,&ledger) ||
        !x86_paging_inventory_capture_lapic(&context,&pool,&mapping,&ledger) ||
        !x86_paging_inventory_verify_lapic(&context,&pool,&mapping,&ledger) ||
        ledger.RamPages != 2 || ledger.MmioPages != 1 || ledger.LeafPages != 3 ||
        ledger.Device.Owner != (BOOT_U64)&mapping || pool.FreePages != 128) return 2;
    if (x86_paging_inventory_verify(&context,&pool,&ledger)) return 3;
    for (bit = 0; bit < 64; ++bit) {
        tables[3][288] ^= 1ULL<<bit;
        if (x86_paging_inventory_verify_lapic(&context,&pool,&mapping,&ledger) != (bit == 5 || bit == 6)) return 4;
        tables[3][288] ^= 1ULL<<bit;
    }
    for (bit = 3096*8; bit < sizeof(ledger)*8; ++bit) {
        ((unsigned char *)&ledger)[bit/8] ^= (unsigned char)(1U<<(bit%8));
        if (x86_paging_inventory_verify_lapic(&context,&pool,&mapping,&ledger)) return 5;
        ((unsigned char *)&ledger)[bit/8] ^= (unsigned char)(1U<<(bit%8));
    }
    save_ledger(); authority_calls = 0; authority_failure = 2;
    if (x86_paging_inventory_capture_lapic(&context,&pool,&mapping,&ledger)) return 6;
    for (bit = 0; bit < sizeof(ledger); ++bit)
        if (((unsigned char *)&ledger)[bit] != ((unsigned char *)&saved)[bit]) return 7;
    authority_failure = 0;
    tables[3][2] = 0x80000000fee00003ULL;
    if (x86_paging_inventory_verify_lapic(&context,&pool,&mapping,&ledger)) return 8;
    tables[3][2] = 0x800000000030001bULL;
    if (x86_paging_inventory_capture_lapic(&context,&pool,&mapping,&ledger)) return 9;
    tables[3][2] = 0;
    if (x86_paging_inventory_capture_lapic(&context,&pool,&mapping,(X86_PAGING_INVENTORY *)&mapping)) return 10;
    map[96] = 11; number(map+104,0xfee00000); number(map+120,1); map[128] = 1;
    context.Info.MemoryMapBytes = 144;
    if (!x86_paging_inventory_verify_lapic(&context,&pool,&mapping,&ledger)) return 11;
    map[96] = 2; map[128] = 8;
    if (x86_paging_inventory_verify_lapic(&context,&pool,&mapping,&ledger)) return 12;
    map[96] = 11; map[128] = 1;
    pool.Free[0].Base = 0xfee00000;
    if (x86_paging_inventory_capture_lapic(&context,&pool,&mapping,&ledger)) return 13;
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
    result = mixed(); if (result) return 600 + result;
    result = video_owned(); if (result) return 700 + result;
    result = video_corruption(); if (result) return 800 + result;
    result = dma_arena(); if (result) return 900 + result;
    result = video_arena(); if (result) return 1000 + result;
    return 0;
}

#include "arch/x86/mm/paging/inventory.h"
#include "arch/x86/mm/devices/lapic.h"
static BOOT_U64 tables[129][512], views[129];
static BOOT_U32 table_count, reads, missing;
static unsigned char map[144];
static KERNEL_BOOT_CONTEXT context;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_PAGING_INVENTORY ledger, saved;
static X86_LAPIC_MAPPING mapping;
static BOOT_U32 authority_bad, authority_calls, authority_failure;
static X86_PAGING_VIDEO_AUTH video;
static BOOT_U32 video_calls, video_failure;

// ====== autorita hardware simulata distinta dalla scansione delle tabelle ===== //

int x86_lapic_authority(const X86_LAPIC_MAPPING *owner)
{
    ++authority_calls;
    return !authority_bad && authority_calls != authority_failure && owner == &mapping &&
        owner->Self == (BOOT_U64)owner && owner->Ready == 1 && owner->Root == 4096 &&
        owner->PhysicalBits == 32 && owner->Physical == 0xfee00000 && owner->Cache == 0x18 &&
        (tables[3][288] & ~0x60ULL) == 0x80000000fee0001bULL;
}

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
    clear(&mapping,sizeof(mapping)); authority_bad = authority_calls = authority_failure = 0;
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

// ====== video_authority ======

static int video_authority(const X86_PAGING_VIDEO_AUTH *owner)
{
    return ++video_calls != video_failure && owner == &video && owner->Root == 4096;
}

// ====== video_fixture ======

static int video_fixture(void)
{
    BOOT_U32 index;
    if (!fixture()) return 0;
    clear(&video, sizeof(video)); video_calls = video_failure = 0;
    mapping.Self = (BOOT_U64)&mapping; mapping.Ready = 1; mapping.Root = 4096;
    mapping.Physical = 0xfee00000; mapping.Cache = 0x18; mapping.PhysicalBits = 32;
    tables[3][288] = 0x80000000fee0001bULL;
    tables[3][1] = 0x8000000000300003ULL;
    video.Self = (BOOT_U64)&video; video.Root = 4096; video.Ready = 1; video.PhysicalBits = 32;
    video.Region.Virtual = (225ULL << 39) + 4096;
    video.Region.Physical = 0xc0000000; video.Region.Pages = 3; video.Region.AperturePages = 8;
    video.Region.Cache = 0x18; video.Region.Pat = 0x0007040600070406ULL;
    video.Region.Owner = (BOOT_U64)&video; video.Region.TableCount = 3;
    context.Info.Framebuffer.Base = video.Region.Physical;
    context.Info.Framebuffer.Capacity = 32768; context.Info.Framebuffer.Used = 12288;
    context.Info.Framebuffer.Width = context.Info.Framebuffer.Stride = 1024;
    context.Info.Framebuffer.Height = 3;
    table_count = 8;
    for (index = 0; index < 3; ++index) {
        if (!kernel_physical_allocate(&pool, 1, &video.Tables[index]) ||
            !kernel_physical_pin(&pool, &video.Tables[index], 1)) return 0;
        if (!index) {
            tables[0][225] = video.Tables[index].Base | 3;
            views[5] = x86_paging_child_view(views[0], 0, 225);
        } else {
            tables[4 + index][0] = video.Tables[index].Base | 3;
            views[5 + index] = x86_paging_child_view(views[4 + index], index, 0);
        }
    }
    for (index = 0; index < 3; ++index)
        tables[7][index + 1] = (video.Region.Physical + (BOOT_U64)index * 4096) | 0x800000000000001bULL;
    return 1;
}

// ====== video_owned ======

static int video_owned(void)
{
    BOOT_U32 index;
    if (!video_fixture() || x86_paging_inventory_capture_lapic(&context, &pool, &mapping, &ledger) ||
        !x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger) ||
        !x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger) ||
        ledger.Count != 8 || ledger.MmioPages != 4 || ledger.RamPages != 2 || pool.FreePages != 125) return 1;
    for (index = 0; index < 3; ++index) {
        BOOT_U32 record, found = 0;
        for (record = 0; record < ledger.Count; ++record)
            if (ledger.Tables[record].Reserved == index + 1 &&
                ledger.Tables[record].Physical == video.Tables[index].Base) ++found;
        if (found != 1) return 2;
    }
    save_ledger(); video_calls = 0; video_failure = 2;
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 3;
    for (index = 0; index < sizeof(ledger); ++index)
        if (((unsigned char *)&ledger)[index] != ((unsigned char *)&saved)[index]) return 4;
    video_failure = 0;
    for (index = 0; index < 3; ++index) {
        if (!kernel_physical_pin(&pool, &video.Tables[index], 0)) return 5;
        reads = 0;
        if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger) || reads) return 6;
        if (!kernel_physical_pin(&pool, &video.Tables[index], 1)) return 7;
    }
    if (!kernel_physical_pin(&pool, &video.Tables[0], 0) ||
        !kernel_physical_release(&pool, &video.Tables[0]) ||
        !kernel_physical_allocate(&pool, 1, &video.Tables[0]) ||
        !kernel_physical_pin(&pool, &video.Tables[0], 1)) return 10;
    if (video.Tables[0].Base != 0x100000 ||
        video.Tables[0].Id == ledger.VideoTableIds[0]) return 12;
    if (x86_paging_inventory_verify_video(&context, &pool, &mapping,
        &video, video_authority, &ledger)) return 11;
    tables[0][225] = 0;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_pin(&pool, &video.Tables[index], 0) ||
            !kernel_physical_release(&pool, &video.Tables[index])) return 8;
    return pool.FreePages == 128 ? 0 : 9;
}

// ====== video_corruption ======

static int video_corruption(void)
{
    BOOT_U32 bit;
    if (!video_fixture() || !x86_paging_inventory_capture_video(&context, &pool, &mapping,
        &video, video_authority, &ledger)) return 1;
    for (bit = 0; bit < 64; ++bit) {
        tables[7][2] ^= 1ULL << bit;
        if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger) !=
            (bit == 5 || bit == 6)) return 2;
        tables[7][2] ^= 1ULL << bit;
    }
    for (bit = 0; bit < 2; ++bit) {
        BOOT_U32 slot = bit ? 4 : 0;
        tables[7][slot] = 0x80000000c000001bULL;
        if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 3;
        tables[7][slot] = 0;
    }
    tables[3][2] = 0x80000000c0007003ULL;
    if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 4;
    tables[3][2] = 0x8000000000100003ULL;
    if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 5;
    tables[3][2] = 0; tables[7][3] = 0;
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 6;
    if (!video_fixture()) return 7;
    video.Tables[1] = video.Tables[0];
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 8;
    if (!video_fixture() || !x86_paging_inventory_capture_video(&context, &pool, &mapping,
        &video, video_authority, &ledger)) return 9;
    for (bit = 0; bit < sizeof(ledger.Video) * 8; ++bit) {
        ((unsigned char *)&ledger.Video)[bit / 8] ^= (unsigned char)(1U << (bit % 8));
        if (x86_paging_inventory_verify_video(&context, &pool, &mapping, &video, video_authority, &ledger)) return 10;
        ((unsigned char *)&ledger.Video)[bit / 8] ^= (unsigned char)(1U << (bit % 8));
    }
    if (x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, 0, &ledger) ||
        x86_paging_inventory_capture_video(&context, &pool, &mapping, &video, video_authority,
            (X86_PAGING_INVENTORY *)&video)) return 11;
    return 0;
}

// ====== dma_arena ======

static int dma_check(const X86_DMA_AUTH *owner) { return owner && (tables[0][X86_DMA_SLOT] & ~0x60ULL) == (owner->Tables[0].Base | 3); }

// Slot 229 arena: three owned tables, two WB/RW/XD leaves; scan, verify, corruption, alias, removal.
static int dma_arena(void)
{
    static X86_DMA_AUTH arena;
    BOOT_U32 index;
    if (!fixture()) return 1;
    // The fixture leaf maps 0x100000, the first free page the arena receives: drop it, then check
    // below that a leaf naming arena RAM outside the slot is refused as an alias.
    tables[3][1] = 0;
    clear(&arena, sizeof(arena));
    if (!kernel_physical_allocate(&pool, 2, &arena.Data) || !kernel_physical_pin(&pool, &arena.Data, 1)) return 2;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_allocate(&pool, 1, &arena.Tables[index]) || !kernel_physical_pin(&pool, &arena.Tables[index], 1)) return 3;
    arena.Self = (BOOT_U64)&arena; arena.Root = 4096; arena.Pat = 6; arena.PhysicalBits = 32; arena.Ready = 1;
    tables[0][X86_DMA_SLOT] = arena.Tables[0].Base | 3;
    views[5] = x86_paging_child_view(views[0], 0, X86_DMA_SLOT);
    tables[5][0] = arena.Tables[1].Base | 3;
    views[6] = x86_paging_child_view(views[5], 1, 0);
    tables[6][0] = arena.Tables[2].Base | 3;
    views[7] = x86_paging_child_view(views[6], 2, 0);
    tables[7][1] = arena.Data.Base | 0x8000000000000003ULL;
    tables[7][2] = (arena.Data.Base + 4096) | 0x8000000000000003ULL;
    table_count = 8;
    // Unregistered tables are not trusted: the scan refuses them.
    if (x86_paging_inventory_capture(&context, &pool, &ledger)) return 4;
    if (!x86_dma_register(0, &arena, dma_check) || !x86_paging_inventory_capture(&context, &pool, &ledger) ||
        ledger.Count != 8 || ledger.LeafPages != 3 || ledger.Dma.Pages != 2 || ledger.Dma.Owner != arena.Self ||
        !x86_paging_inventory_verify(&context, &pool, &ledger)) return 5;
    {
        BOOT_U32 tags = 0;
        for (index = 0; index < ledger.Count; ++index)
            if (ledger.Tables[index].Reserved >= 40) tags |= 1U << (ledger.Tables[index].Reserved - 40);
        if (tags != 7) return 14;
    }
    tables[7][2] |= 0x10;
    if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 6;
    tables[7][2] &= ~0x10ULL; tables[7][3] = (arena.Data.Base + 8192) | 0x8000000000000003ULL;
    if (x86_paging_inventory_verify(&context, &pool, &ledger)) return 7;
    tables[7][3] = 0; tables[3][2] = arena.Data.Base | 0x8000000000000003ULL;
    if (x86_paging_inventory_capture(&context, &pool, &ledger) || x86_paging_inventory_verify(&context, &pool, &ledger)) return 8;
    tables[3][2] = 0;
    if (!x86_paging_inventory_capture(&context, &pool, &ledger) || !x86_dma_register(&arena, 0, 0) ||
        x86_paging_inventory_verify(&context, &pool, &ledger)) return 9;
    tables[0][X86_DMA_SLOT] = 0; table_count = 5;
    if (!x86_paging_inventory_capture(&context, &pool, &ledger) || ledger.Dma.Owner) return 10;
    for (index = 0; index < 3; ++index)
        if (!kernel_physical_pin(&pool, &arena.Tables[index], 0) || !kernel_physical_release(&pool, &arena.Tables[index])) return 11;
    if (!kernel_physical_pin(&pool, &arena.Data, 0) || !kernel_physical_release(&pool, &arena.Data)) return 12;
    return pool.FreePages == 128 ? 0 : 13;
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
    return 0;
}

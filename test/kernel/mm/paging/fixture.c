#include "fixture.h"
BOOT_U64 tables[129][512], views[129];
BOOT_U32 table_count, reads, missing;
unsigned char map[144];
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGING_INVENTORY ledger, saved;
X86_LAPIC_MAPPING mapping;
BOOT_U32 authority_bad, authority_calls, authority_failure;
X86_PAGING_VIDEO_AUTH video;
BOOT_U32 video_calls, video_failure;
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

void clear(void *object, BOOT_U64 bytes)
{
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)object)[index] = 0;
}

// ====== descrittori little endian con riserva loader separata dalla RAM libera ===== //

void number(unsigned char *buffer, BOOT_U64 value)
{
    BOOT_U32 index;
    for (index = 0; index < 8; ++index) buffer[index] = (unsigned char)(value >> (8 * index));
}

// ====== copia bounded per preservare la fotografia senza memcpy implicita ===== //

void save_ledger(void)
{
    BOOT_U32 index;
    for (index = 0; index < sizeof(ledger); ++index)
        ((volatile unsigned char *)&saved)[index] = ((const unsigned char *)&ledger)[index];
}

// ====== grafo di quattro livelli piu un ramo distinto nello stesso PD ===== //

int fixture(void)
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
    views[0] = X86_LAYOUT_RECURSIVE_VIEW(X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT);
    for (level = 0; level < 3; ++level) {
        BOOT_U32 slot = level ? 0 : X86_LAYOUT_WINDOWS_SLOT;
        tables[level][slot] = ((BOOT_U64)level + 2) * 4096 | 3;
        views[level + 1] = x86_paging_child_view(views[level], level, slot);
    }
    tables[0][X86_LAYOUT_RECURSIVE_SLOT] = 0x8000000000001003ULL;
    tables[2][1] = 0x5003;
    views[4] = x86_paging_child_view(views[2], 2, 1);
    tables[3][1] = 0x8000000000100003ULL;
    tables[4][511] = 0x200001;
    return kernel_physical_initialize(&pool, map, 96, 48, 32);
}

#include "arch/x86/mm/arena/objects.h"
static X86_OBJECT_ARENA arena, copied;
static KERNEL_OBJECT_HANDLE handles[64];
int x86_test_region_fixture(void);
X86_REGION_MAPPER *x86_test_region_mapper(void);
KERNEL_PHYSICAL_ALLOCATOR *x86_test_region_pool(void);
void x86_test_region_offset(BOOT_U32 value);

// ====== reset volatile senza dipendenze da CRT ===== //

static void clear(void *object, BOOT_U64 bytes)
{
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)object)[index] = 0;
}

// ====== binding del pool protegge anche copie dell'handle del mapping ===== //

static int lifecycle(void)
{
    X86_REGION_HANDLE region = {0}, duplicate;
    KERNEL_OBJECT_HANDLE object = {0}, stale;
    X86_REGION_MAPPER *mapper;
    unsigned char *data;
    BOOT_U32 byte;
    clear(&arena, sizeof(arena)); clear(&copied, sizeof(copied));
    if (!x86_test_region_fixture()) return 1;
    mapper = x86_test_region_mapper();
    if (!x86_region_acquire(mapper, 16, 1, &region) || !x86_object_arena_attach(&arena, mapper, &region)) return 2;
    duplicate = region;
    if (x86_region_release(mapper, &duplicate) || x86_region_detach(mapper) ||
        x86_object_arena_attach(&copied, mapper, &region) || !x86_object_arena_validate(&arena)) return 3;
    if (!x86_object_arena_allocate(&arena, 4097, 4096, &object)) return 4;
    data = x86_object_arena_data(&arena, &object);
    if (!data || ((BOOT_U64)data & 4095)) return 5;
    for (byte = 0; byte < object.Bytes; ++byte) { if (data[byte]) return 6; data[byte] = 0xa5; }
    data = 0; stale = object;
    if (x86_object_arena_detach(&arena) || !x86_object_arena_release(&arena, &object) ||
        x86_object_arena_data(&arena, &stale) || !x86_object_arena_detach(&arena) ||
        !x86_region_release(mapper, &region)) return 7;
    if (!x86_region_acquire(mapper, 16, 1, &region) || !x86_object_arena_attach(&arena, mapper, &region) ||
        !x86_object_arena_allocate(&arena, 4097, 4096, &object) || object.Id <= stale.Id ||
        x86_object_arena_data(&arena, &stale) || x86_object_arena_release(&arena, &stale)) return 8;
    data = x86_object_arena_data(&arena, &object);
    for (byte = 0; byte < object.Bytes; ++byte) if (data[byte]) return 9;
    data = 0;
    if (!x86_object_arena_release(&arena, &object) || !x86_object_arena_detach(&arena) ||
        !x86_region_release(mapper, &region) || x86_test_region_pool()->FreePages != 128) return 10;
    return 0;
}

// ====== saturazione record e bytes senza consumo di ulteriori frame fisici ===== //

static int exhaustion(void)
{
    X86_REGION_HANDLE region = {0};
    X86_REGION_MAPPER *mapper;
    KERNEL_OBJECT_HANDLE extra = {0};
    BOOT_U32 slot;
    clear(&arena, sizeof(arena)); clear(handles, sizeof(handles));
    if (!x86_test_region_fixture()) return 1;
    mapper = x86_test_region_mapper();
    if (!x86_region_acquire(mapper, 16, 1, &region) || !x86_object_arena_attach(&arena, mapper, &region)) return 2;
    for (slot = 0; slot < 64; ++slot) if (!x86_object_arena_allocate(&arena, 32, 32, &handles[slot])) return 3;
    if (x86_object_arena_allocate(&arena, 1, 1, &extra) || extra.Id || arena.Objects.Used != 2048 ||
        x86_test_region_pool()->FreePages != 112) return 4;
    for (slot = 0; slot < 64; ++slot) if (!x86_object_arena_release(&arena, &handles[slot])) return 5;
    if (!x86_object_arena_allocate(&arena, 65536, 4096, &extra) ||
        x86_object_arena_allocate(&arena, 1, 1, &handles[0]) || handles[0].Id ||
        !x86_object_arena_release(&arena, &extra) || !x86_object_arena_detach(&arena) ||
        !x86_region_release(mapper, &region)) return 6;
    return 0;
}

// ====== owner copiato e output alias non possono alterare il ciclo di vita ===== //

static int rejected(void)
{
    X86_REGION_HANDLE region = {0}, changed;
    X86_REGION_MAPPER *mapper;
    KERNEL_OBJECT_HANDLE handle = {0};
    BOOT_U32 byte;
    clear(&arena, sizeof(arena)); clear(&copied, sizeof(copied));
    if (!x86_test_region_fixture()) return 1;
    mapper = x86_test_region_mapper();
    if (!x86_region_acquire(mapper, 32, 1, &region) || x86_object_arena_attach(&arena, mapper, &region) ||
        arena.Ready || !x86_region_release(mapper, &region)) return 2;
    if (!x86_region_acquire(mapper, 16, 1, &region)) return 3;
    x86_test_region_offset(1);
    if (x86_object_arena_attach(&arena, mapper, &region) || mapper->Borrowers[0] || arena.Ready || arena.Objects.Ready) return 8;
    x86_test_region_offset(0);
    changed = region; ++changed.Id;
    if (x86_object_arena_attach(&arena, mapper, &changed) || arena.Ready ||
        !x86_object_arena_attach(&arena, mapper, &region)) return 4;
    for (byte = 0; byte < sizeof(copied); ++byte) ((volatile unsigned char *)&copied)[byte] = ((const unsigned char *)&arena)[byte];
    if (x86_object_arena_validate(&copied) || x86_object_arena_allocate(&copied, 1, 1, &handle) ||
        x86_object_arena_allocate(&arena, 1, 1, (KERNEL_OBJECT_HANDLE *)&arena.Region) ||
        x86_object_arena_allocate(&arena, 1, 1, (KERNEL_OBJECT_HANDLE *)&mapper->Live[0]) || arena.Objects.Count) return 5;
    arena.Region.Id ^= 1;
    if (x86_object_arena_allocate(&arena, 1, 1, &handle) || x86_object_arena_data(&arena, &handle)) return 6;
    arena.Region.Id ^= 1;
    if (!x86_object_arena_detach(&arena) || !x86_region_release(mapper, &region)) return 7;
    return 0;
}

// ====== riuso di un pool persistente con budget fisico costante ===== //

static int cycles(void)
{
    X86_REGION_HANDLE region = {0};
    X86_REGION_MAPPER *mapper;
    KERNEL_OBJECT_HANDLE first = {0}, second = {0};
    BOOT_U32 round, byte;
    clear(&arena, sizeof(arena));
    if (!x86_test_region_fixture()) return 1;
    mapper = x86_test_region_mapper();
    if (!x86_region_acquire(mapper, 16, 1, &region) || !x86_object_arena_attach(&arena, mapper, &region)) return 2;
    for (round = 0; round < 256; ++round) {
        unsigned char *data;
        if (!x86_object_arena_allocate(&arena, 17 + round, 64, &first) ||
            !x86_object_arena_allocate(&arena, 4097, 4096, &second)) return 3;
        data = x86_object_arena_data(&arena, &first);
        if (!data) return 4;
        for (byte = 0; byte < first.Bytes; ++byte) { if (data[byte]) return 5; data[byte] = 0x5a; }
        data = 0;
        if (!x86_object_arena_release(&arena, &first) || !x86_object_arena_release(&arena, &second) ||
            arena.Objects.Used || !x86_object_arena_validate(&arena) || x86_test_region_pool()->FreePages != 112) return 6;
    }
    return x86_object_arena_detach(&arena) && x86_region_release(mapper, &region) ? 0 : 7;
}

// ====== handle alterati e metadati discordanti respinti prima di accedere al buffer ===== //

static int corruptions(void)
{
    X86_REGION_HANDLE region = {0};
    X86_REGION_MAPPER *mapper;
    KERNEL_OBJECT_HANDLE handle = {0}, changed;
    BOOT_U32 byte, bit;
    unsigned char *saved;
    clear(&arena, sizeof(arena));
    if (!x86_test_region_fixture()) return 1;
    mapper = x86_test_region_mapper();
    if (!x86_region_acquire(mapper, 16, 1, &region) || !x86_object_arena_attach(&arena, mapper, &region) ||
        !x86_object_arena_allocate(&arena, 64, 64, &handle)) return 2;
    for (byte = 0; byte < sizeof(handle); ++byte) {
        for (bit = 0; bit < 8; ++bit) {
            changed = handle;
            ((unsigned char *)&changed)[byte] ^= (unsigned char)(1U << bit);
            if (x86_object_arena_data(&arena, &changed) || x86_object_arena_release(&arena, &changed) ||
                arena.Objects.Count != 1 || arena.Objects.Used != 64 || x86_test_region_pool()->FreePages != 112) return 3;
        }
    }
    saved = arena.Objects.Arena; arena.Objects.Arena += 4096;
    if (x86_object_arena_data(&arena, &handle) || x86_object_arena_release(&arena, &handle)) return 4;
    arena.Objects.Arena = saved;
    arena.Reserved = 1;
    if (x86_object_arena_data(&arena, &handle)) return 5;
    arena.Reserved = 0; mapper->Borrowers[0] = 0;
    if (x86_object_arena_data(&arena, &handle) || x86_object_arena_detach(&arena)) return 6;
    mapper->Borrowers[0] = (BOOT_U64)&arena;
    if (!x86_object_arena_release(&arena, &handle) || !x86_object_arena_detach(&arena)) return 7;
    arena.Objects.NextId = 0;
    if (x86_object_arena_attach(&arena, mapper, &region) || mapper->Borrowers[0]) return 8;
    arena.Objects.NextId = 2;
    if (!x86_object_arena_attach(&arena, mapper, &region) || !x86_object_arena_detach(&arena) ||
        !x86_region_release(mapper, &region)) return 9;
    return 0;
}

// ====== sessantaquattro riaperture mantengono gli ID e nessuna lease residua ===== //

static int reopen_cycles(void)
{
    X86_REGION_HANDLE region = {0};
    X86_REGION_MAPPER *mapper;
    KERNEL_OBJECT_HANDLE handle = {0}, stale = {0};
    BOOT_U32 round;
    clear(&arena, sizeof(arena));
    if (!x86_test_region_fixture()) return 1;
    mapper = x86_test_region_mapper();
    if (!x86_region_acquire(mapper, 16, 1, &region)) return 2;
    for (round = 0; round < 64; ++round) {
        if (!x86_object_arena_attach(&arena, mapper, &region) ||
            !x86_object_arena_allocate(&arena, 512, 64, &handle) || handle.Id <= stale.Id ||
            x86_object_arena_data(&arena, &stale)) return 3;
        stale = handle;
        if (!x86_object_arena_release(&arena, &handle) || !x86_object_arena_detach(&arena) ||
            mapper->Borrowers[0] || x86_test_region_pool()->FreePages != 112) return 4;
    }
    return x86_region_release(mapper, &region) && x86_test_region_pool()->FreePages == 128 ? 0 : 5;
}

// ====== entry host per il binding oggetti mapping ===== //

__declspec(dllexport) int run_arena_tests(void)
{
    int result = lifecycle();
    if (result) return 100 + result;
    result = exhaustion(); if (result) return 200 + result;
    result = rejected(); if (result) return 300 + result;
    result = cycles(); if (result) return 400 + result;
    result = corruptions(); if (result) return 500 + result;
    result = reopen_cycles(); return result ? 600 + result : 0;
}

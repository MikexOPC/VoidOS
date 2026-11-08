#include "arch/x86/mm/paging/retirement/retire.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ROOT_VIEW X86_LAYOUT_RECURSIVE_VIEW(510, 510, 510, 510)
#define NX (1ULL << 63)
static KERNEL_BOOT_CONTEXT context;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_RETIRE_CPU cpu;
static X86_PAGING_INVENTORY snapshot;
static BOOT_U64 root[512], pt[512], generation, pt_view;
static unsigned char map[80];
static unsigned int commits, captures, fault;

// ====== fixture_private ======

static void fixture_private(void) {
    memset(&context, 0, sizeof(context));
    memset(&pool, 0, sizeof(pool));
    memset(&snapshot, 0, sizeof(snapshot));
    memset(root, 0, sizeof(root));
    memset(pt, 0, sizeof(pt));
    memset(map, 0, sizeof(map));
    cpu = (X86_RETIRE_CPU){0x80010000, 0x400000, 0x20, 0xd00, 2,
        X86_KERNEL_IMAGE_BASE + 256, X86_KERNEL_MAIN_BASE + 4096,
        X86_KERNEL_IMAGE_BASE + 4096, 55, X86_KERNEL_IMAGE_BASE + 8192, 511, 24, 8, 16, 0};
    context.MemoryMap = map;
    context.Info.MemoryMapBytes = sizeof(map);
    context.Info.MemoryDescriptorBytes = 40;
    context.Info.PageTableRoot = cpu.Root;
    BOOT_U32 types[2] = {1, 2};
    BOOT_U64 bases[2] = {0x10000, 0x20000}, pages[2] = {1, 11}, attributes = 8;
    for (unsigned int i = 0; i < 2; ++i) {
        memcpy(map + i * 40, &types[i], 4);
        memcpy(map + i * 40 + 8, &bases[i], 8);
        memcpy(map + i * 40 + 24, &pages[i], 8);
        memcpy(map + i * 40 + 32, &attributes, 8);
    }
    root[0] = 0x401003;
    root[510] = cpu.Root | 3 | NX;
    root[511] = 0x405003;
    snapshot.Root = cpu.Root;
    snapshot.Count = 5;
    snapshot.Ready = 1;
    snapshot.LeafPages = 26;
    snapshot.Tables[0] = (X86_PAGING_RESOURCE){cpu.Root, ROOT_VIEW, 0, 0};
    BOOT_U64 view = ROOT_VIEW;
    for (BOOT_U32 i = 1; i <= 3; ++i) {
        view = x86_paging_child_view(view, i - 1, 0);
        snapshot.Tables[i] = (X86_PAGING_RESOURCE){cpu.Root + i * 4096, view, i, 0};
    }
    pt_view = view;
    snapshot.Tables[4] = (X86_PAGING_RESOURCE){0x405000,
        x86_paging_child_view(ROOT_VIEW, 0, 511), 1, 0};
    pt[16] = 0x10001;
    for (BOOT_U32 i = 0; i < 11; ++i)
        if (i != 1 && i != 6) pt[32 + i] = (0x20000 + i * 4096) | 3 | NX;
    pool.FreePages = 100;
    commits = captures = fault = 0;
    generation = 7;
}

// ====== backend_mocks ======

int x86_retire_cpu(X86_RETIRE_CPU *output) { *output = cpu; return fault != 1; }
int x86_retire_loaded(void) { return fault != 2; }
volatile BOOT_U64 *x86_retire_table(BOOT_U64 view) {
    if (fault == 3) return NULL;
    return view == ROOT_VIEW ? root : view == pt_view ? pt : NULL;
}
int x86_paging_inventory_capture(const KERNEL_BOOT_CONTEXT *boot,
    const KERNEL_PHYSICAL_ALLOCATOR *owner, X86_PAGING_INVENTORY *output) {
    assert(boot == &context && owner == &pool);
    ++captures;
    if (fault == 4 || (fault == 5 && commits)) return 0;
    *output = snapshot;
    if (commits) { output->Count -= 3; output->LeafPages -= 10; }
    if (fault == 6 && commits) ++output->LeafPages;
    return 1;
}
int x86_retire_commit(volatile BOOT_U64 *table, BOOT_U64 expected,
    BOOT_U64 entry, BOOT_U64 cr4) {
    assert(table == root && expected == cpu.Root && cr4 == cpu.Cr4 && root[0] == entry);
    if (fault == 7) return 0;
    root[0] = 0;
    ++commits;
    if (fault == 8) root[511] ^= 4096;
    if (fault == 9) ++pool.FreePages;
    return 1;
}
BOOT_U64 x86_paging_generation(void) { return generation; }
void x86_paging_generation_bump(void) { if (fault != 10) ++generation; }
int kernel_text(const char *text) { assert(text); return 1; }
int kernel_number(BOOT_U64 value) { (void)value; return 1; }
int kernel_log(unsigned int level, const char *text) { (void)level; assert(text); return 1; }

// ====== reject_private ======

static void reject_private(void) {
    BOOT_U64 original = root[0];
    assert(!x86_paging_retire_loader(&context, &pool));
    assert(!commits && root[0] == original && generation == 7);
}

// ====== main ======

int main(void) {
    fixture_private();
    assert(x86_retire_cpu_valid(&cpu, cpu.Root));
    assert(!x86_retire_cpu_valid(NULL, cpu.Root));
    assert(!x86_retire_cpu_valid(&cpu, 0));
    assert(!x86_retire_cpu_valid(&cpu, cpu.Root + 1));
    for (unsigned int i = 0; i < 64; ++i) {
        fixture_private();
        cpu.Cr4 |= 1ULL << i;
        if (i == 7 || i == 12 || i == 17) reject_private();
    }
    const struct { size_t offset; BOOT_U64 value; } bad[] = {
        {offsetof(X86_RETIRE_CPU, Cr0), 0x80000000},
        {offsetof(X86_RETIRE_CPU, Cr0), 0x10000},
        {offsetof(X86_RETIRE_CPU, Cr4), 0},
        {offsetof(X86_RETIRE_CPU, Root), 0x401000},
        {offsetof(X86_RETIRE_CPU, Efer), 0x500},
        {offsetof(X86_RETIRE_CPU, Flags), 0},
        {offsetof(X86_RETIRE_CPU, Flags), 0x202},
        {offsetof(X86_RETIRE_CPU, Flags), 0x3002},
        {offsetof(X86_RETIRE_CPU, Flags), 0x4002},
        {offsetof(X86_RETIRE_CPU, Flags), 0x20002},
        {offsetof(X86_RETIRE_CPU, Rip), 0x10000},
        {offsetof(X86_RETIRE_CPU, Rsp), X86_KERNEL_MAIN_TOP},
        {offsetof(X86_RETIRE_CPU, Rsp), X86_KERNEL_MAIN_BASE - 1},
        {offsetof(X86_RETIRE_CPU, GdtBase), 0x20000},
        {offsetof(X86_RETIRE_CPU, GdtBase), X86_KERNEL_IMAGE_LIMIT - 39},
        {offsetof(X86_RETIRE_CPU, IdtBase), X86_KERNEL_IMAGE_LIMIT - 511},
        {offsetof(X86_RETIRE_CPU, GdtLimit), 40},
        {offsetof(X86_RETIRE_CPU, IdtLimit), 510},
        {offsetof(X86_RETIRE_CPU, Tr), 16},
        {offsetof(X86_RETIRE_CPU, Cs), 0},
        {offsetof(X86_RETIRE_CPU, Ss), 0},
        {offsetof(X86_RETIRE_CPU, Ldt), 8}
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        fixture_private();
        memcpy((unsigned char *)&cpu + bad[i].offset, &bad[i].value, 8);
        reject_private();
    }
    for (BOOT_U32 slot = 1; slot < 256; ++slot) {
        fixture_private(); root[slot] = 3; reject_private();
    }
    BOOT_U64 bad_entries[] = {0, 0x10003, 0x11001, 0x10101, 0x10005, 0x10081};
    for (unsigned int i = 0; i < sizeof(bad_entries) / sizeof(*bad_entries); ++i) {
        fixture_private(); pt[16] = bad_entries[i]; reject_private();
    }
    fixture_private(); pt[33] = 0x21000 | NX | 3; reject_private();
    fixture_private(); pt[34] = 0; reject_private();
    fixture_private(); map[0] = 7; reject_private();
    fixture_private(); map[32] = 0; reject_private();
    fixture_private(); map[72] = 0; reject_private();
    fixture_private(); pool.Count = 1; pool.Free[0] = (KERNEL_PHYSICAL_EXTENT){0x10000, 1}; reject_private();
    fixture_private(); pool.Live[0] = (KERNEL_PHYSICAL_LEASE){0x21000, 1, 1, 1}; reject_private();
    fixture_private(); context.Info.MemoryDescriptorBytes = 0; reject_private();
    fixture_private(); pool.Count = KERNEL_PHYSICAL_EXTENTS + 1; reject_private();
    for (unsigned int i = 1; i <= 10; ++i) {
        fixture_private(); fault = i;
        if (i <= 4 || i == 7) reject_private();
        else {
            assert(!x86_paging_retire_loader(&context, &pool));
            assert(commits == 1 && root[0] == 0);
        }
    }
    for (unsigned int i = 0; i < 1000; ++i) {
        fixture_private();
        assert(x86_paging_retire_loader(&context, &pool));
        assert(commits == 1 && captures == 2 && !root[0] && generation == 8 && pool.FreePages == 100);
        assert(!x86_paging_retire_loader(&context, &pool));
    }
    puts("PASS retirement: 22 CPU cases, 255 low slots, ownership/failure paths, 1000 success/reentry cycles (mock commit)");
    return 0;
}

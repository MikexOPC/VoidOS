#include "mock.h"
KERNEL_BOOT_CONTEXT context;
KERNEL_PHYSICAL_ALLOCATOR pool;
X86_PAGE_WINDOW window;
X86_PAGING_INVENTORY inventory;
X86_DMA_ARENA arena;
BOOT_U64 roots[512];
unsigned checks, failure, borrows;
__declspec(align(4096)) static BOOT_U64 frames[64][512];
static unsigned char memory_map[48];
static BOOT_U64 *borrowed;

// ====== arena_capture ======

// Independent model of the inventory scan: table records, exact entries and guards, completion.
int arena_capture(const KERNEL_BOOT_CONTEXT *boot, const KERNEL_PHYSICAL_ALLOCATOR *allocator,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *out)
{
    (void)lapic;

    if (failure == 3 && x86_dma_owner()) return 0;

    memset(out, 0, sizeof(*out));
    out->Root = boot->Info.PageTableRoot; out->Count = 1; out->Ready = 1;
    out->Tables[0] = (X86_PAGING_RESOURCE){0x400000, 0x400000, 0, 0};

    if (!x86_dma_inputs(allocator, out->Root, (BOOT_U64)out, sizeof(*out), &out->Dma)) return 0;

    const X86_DMA_AUTH *auth = x86_dma_owner();

    if (!auth) return !roots[X86_DMA_SLOT];

    BOOT_U64 leaves = 0;

    for (BOOT_U32 table = 0; table < 3; ++table) {
        const BOOT_U64 *page = (const BOOT_U64 *)auth->Tables[table].Base;
        BOOT_U32 level = table + 1;
        BOOT_U64 view = (BOOT_U64)X86_DMA_SLOT << (12 + (level - 1) * 9);

        out->Tables[out->Count++] = (X86_PAGING_RESOURCE){auth->Tables[table].Base, view, level, 40 + table};
        CHECK(x86_dma_record(&out->Dma, auth->Tables[table].Base, view, level, 40 + table));

        for (BOOT_U32 slot = 0; slot < 512; ++slot) {
            BOOT_U64 expected = 0;

            if (table < 2 && !slot) expected = auth->Tables[table + 1].Base | 3;

            if (table == 2 && slot && slot <= auth->Data.Pages) {
                expected = (auth->Data.Base + (slot - 1) * 4096ULL) | 0x8000000000000003ULL;
                CHECK(x86_dma_leaf(page[slot], X86_DMA_VIRTUAL + (slot - 1) * 4096ULL) == 8);
                ++leaves;
            }

            if (page[slot] != expected) return 0;
        }
    }

    return x86_dma_complete(&out->Dma, 7, leaves);
}

// ====== arena_frame_byte ======

BOOT_U64 arena_frame_byte(BOOT_U64 physical)
{
    return *(const BOOT_U64 *)physical;
}

// ====== arena_fixture ======

void arena_fixture(void)
{
    CHECK(!x86_dma_owner());
    failure = 0; borrowed = 0; borrows = 0;
    memset(&context, 0, sizeof(context)); memset(&pool, 0, sizeof(pool)); memset(&window, 0, sizeof(window));
    memset(&inventory, 0, sizeof(inventory)); memset(&arena, 0, sizeof(arena)); memset(roots, 0, sizeof(roots));
    memset(memory_map, 0, sizeof(memory_map)); memory_map[0] = 7;

    // Stale contents: binding must zero every arena page before publishing it.
    for (BOOT_U32 page = 0; page < 64; ++page) for (BOOT_U32 slot = 0; slot < 512; ++slot) frames[page][slot] = 0xa5a5a5a5a5a5a5a5ULL;

    for (BOOT_U32 byte = 0; byte < 8; ++byte) {
        memory_map[8 + byte] = (unsigned char)((BOOT_U64)frames >> (byte * 8));
        memory_map[24 + byte] = (unsigned char)(64ULL >> (byte * 8));
    }

    memory_map[32] = 8;
    CHECK(kernel_physical_initialize(&pool, memory_map, 48, 48, 52));
    context.Info.PageTableRoot = 0x400000; window.Pool = &pool; window.Root = 0x400000;
}

// ====== mocked CPU, PML4 and window ======

int x86_fb_cpu(X86_FB_CPU *cpu) { *cpu = (X86_FB_CPU){0x400000, 6, 52, 0}; return failure != 4; }
volatile BOOT_U64 *x86_fb_root(void) { return roots; }
int x86_fb_flush(BOOT_U64 root) { CHECK(root == 0x400000); return failure != 1; }
int x86_page_window_idle_valid(const X86_PAGE_WINDOW *loan) { return loan == &window && !borrowed; }
const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address) { CHECK(address == 0x400000); return roots; }
volatile unsigned char *x86_window_bytes(void) { return (volatile unsigned char *)borrowed; }

int x86_apic_borrow(X86_PAGE_WINDOW *loan, BOOT_U64 physical, BOOT_U64 cache, int writable, int (*operation)(void *), void *opaque)
{
    CHECK(loan == &window && !cache && writable == 1 && !borrowed && !(physical & 4095));

    if (failure == 2 || (failure >= 10 && ++borrows == failure - 9)) return 0;

    borrowed = (BOOT_U64 *)physical;
    int result = operation(opaque);
    borrowed = 0;
    return result;
}

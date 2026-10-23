#include "mock.h"

// ====== bind_cases ======

static void bind_cases(BOOT_U32 pages)
{
    arena_fixture();
    BOOT_U64 budget = pool.FreePages;

    CHECK(x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, pages, &arena));
    CHECK(arena.Authority.Data.Pages == pages && pool.FreePages == budget - pages - 3 && x86_dma_owner() == &arena.Authority);
    CHECK(roots[X86_DMA_SLOT] == (arena.Authority.Tables[0].Base | 3) && kernel_physical_is_pinned(&pool, &arena.Authority.Data));
    CHECK(!kernel_physical_release(&pool, &arena.Authority.Data));
    CHECK(x86_dma_arena_bytes(&arena) == (volatile unsigned char *)X86_DMA_VIRTUAL);

    // Every page was zeroed through the window before publication.
    for (BOOT_U64 page = 0; page < pages; ++page) CHECK(!arena_frame_byte(arena.Authority.Data.Base + page * 4096));

    // Leaf policy: exact WB/RW/XD entries inside the slot, guards and aliases refused.
    BOOT_U64 base = arena.Authority.Data.Base;
    CHECK(x86_dma_leaf(base | 0x8000000000000003ULL, X86_DMA_VIRTUAL) == 8);
    CHECK(x86_dma_leaf((base + (pages - 1) * 4096ULL) | 0x8000000000000063ULL, X86_DMA_VIRTUAL + (pages - 1) * 4096ULL) == 8);
    CHECK(!x86_dma_leaf(base | 0x8000000000000003ULL, X86_DMA_VIRTUAL - 4096));
    CHECK(!x86_dma_leaf((base + pages * 4096ULL) | 0x8000000000000003ULL, X86_DMA_VIRTUAL + pages * 4096ULL));
    CHECK(!x86_dma_leaf(base | 3, X86_DMA_VIRTUAL) && !x86_dma_leaf(base | 0x8000000000000007ULL, X86_DMA_VIRTUAL));
    CHECK(!x86_dma_leaf(base | 0x8000000000000013ULL, X86_DMA_VIRTUAL) && !x86_dma_leaf(base | 0x8000000000000003ULL, X86_DMA_VIRTUAL + 1));
    CHECK(!x86_dma_leaf(base | 0x8000000000000003ULL, 0x5000) && x86_dma_leaf(0x200003, 0x5000) == 1);

    // Snapshot integrity and table records.
    X86_DMA_SNAPSHOT snapshot = inventory.Dma;
    CHECK(x86_dma_complete(&snapshot, 7, pages) && !x86_dma_complete(&snapshot, 3, pages) && !x86_dma_complete(&snapshot, 7, pages + 1));
    ++snapshot.DataId; CHECK(!x86_dma_complete(&snapshot, 7, pages)); --snapshot.DataId;
    ++snapshot.TableIds[2]; CHECK(!x86_dma_complete(&snapshot, 7, pages)); --snapshot.TableIds[2];
    CHECK(!x86_dma_record(&snapshot, arena.Authority.Tables[0].Base, (BOOT_U64)X86_DMA_SLOT << 12, 2, 40));
    CHECK(!x86_dma_record(&snapshot, arena.Authority.Tables[1].Base, (BOOT_U64)X86_DMA_SLOT << 21, 2, 43));
    CHECK(x86_dma_source(&pool, arena.Authority.Tables[2].Base) == 43 && !x86_dma_source(&pool, base));
    CHECK(kernel_physical_pin(&pool, &arena.Authority.Data, 0));
    CHECK(!x86_dma_inputs(&pool, 0x400000, (BOOT_U64)&snapshot, sizeof(snapshot), &snapshot));
    CHECK(kernel_physical_pin(&pool, &arena.Authority.Data, 1));
    CHECK(!x86_dma_inputs(&pool, 0x500000, 0, 0, &snapshot) && !x86_dma_inputs(&pool, 0x400000, 0, 0, 0));
    CHECK(!x86_dma_inputs(&pool, 0x400000, (BOOT_U64)&arena.Authority, sizeof(arena.Authority), &snapshot));

    // A second owner and a foreign PML4 entry are refused; the registry stays with the first.
    X86_DMA_ARENA other = {0};
    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 1, &other) && !other.Self);
    BOOT_U64 actual = roots[X86_DMA_SLOT]; roots[X86_DMA_SLOT] = 0xdead0003;
    CHECK(!x86_dma_arena_bytes(&arena) && !x86_dma_arena_unbind(&arena) && pool.FreePages == budget - pages - 3);
    roots[X86_DMA_SLOT] = actual;

    CHECK(x86_dma_arena_unbind(&arena) && pool.FreePages == budget && !roots[X86_DMA_SLOT] && !x86_dma_owner() && !arena.Self);
    CHECK(!x86_dma_arena_unbind(&arena) && !x86_dma_arena_bytes(&arena));
}

// ====== refusal_cases ======

static void refusal_cases(void)
{
    arena_fixture();
    BOOT_U64 budget = pool.FreePages;

    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 0, &arena));
    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, X86_DMA_PAGES + 1, &arena));
    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, 0, 4, &arena));
    CHECK(!x86_dma_arena_bind(0, &window, 0, &inventory, arena_capture, 4, &arena));
    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, (X86_DMA_ARENA *)&pool));
    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, (X86_DMA_ARENA *)((char *)&arena + 4)));
    roots[X86_DMA_SLOT] = 3; CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, &arena)); roots[X86_DMA_SLOT] = 0;
    arena.Budget = 1; CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, &arena)); arena.Budget = 0;
    failure = 4; CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, &arena)); failure = 0;
    CHECK(pool.FreePages == budget && !x86_dma_owner() && !arena.Self);

    // Exhausted pool: the arena needs its pages plus three tables.
    KERNEL_PHYSICAL_LEASE busy = {0};
    CHECK(kernel_physical_allocate(&pool, budget - 6, &busy));
    CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, &arena) && !arena.Self);
    CHECK(kernel_physical_release(&pool, &busy) && pool.FreePages == budget);
}

// ====== rollback_cases ======

// Flush, construction borrows (every position) and the final capture fail: full rollback each time.
static void rollback_cases(void)
{
    const unsigned failures[] = {1, 2, 3, 10, 11, 12, 13, 14, 15, 16};

    for (unsigned index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        arena_fixture();
        BOOT_U64 budget = pool.FreePages;

        failure = failures[index];
        CHECK(!x86_dma_arena_bind(&context, &window, 0, &inventory, arena_capture, 4, &arena));

        if (arena.Self) {
            // Flush failure leaves the slot published: unbinding needs a working flush.
            failure = 0;
            CHECK(x86_dma_arena_unbind(&arena));
        }

        failure = 0;
        CHECK(pool.FreePages == budget && !roots[X86_DMA_SLOT] && !x86_dma_owner() && !arena.Self);
    }
}

// ====== main ======

int main(void)
{
    for (unsigned cycle = 0; cycle < 50; ++cycle)
        for (BOOT_U32 pages = 1; pages <= X86_DMA_PAGES; ++pages) bind_cases(pages);

    refusal_cases();
    rollback_cases();
    printf("PASS %u DMA arena checks; 800 bind cycles of 1-16 pages, refusals, aliases, guards and rollback\n", checks);
    return 0;
}

#include "kernel/mm/heap/heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(value) do { ++checks; if (!(value)) { printf("FAIL heap line %d\n", __LINE__); exit(1); } } while (0)

static __declspec(align(4096)) unsigned char arena[KERNEL_HEAP_MAX_PAGES * 4096ULL];
static KERNEL_HEAP heap;

// ====== reset_private_fixture ======

static int reset_private_fixture(uint32_t pages) {
    memset(&heap, 0, sizeof(heap));
    return kernel_heap_init(&heap, arena, pages);
}

// ====== next_private ======

static uint32_t state = 0x9e3779b9U;

static uint32_t next_private(void)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

// ====== oracle ======

// Live allocations with their size and the byte pattern written into them.
#define LIVE 2048U
static struct { unsigned char *Pointer; uint64_t Bytes; unsigned char Pattern; } live[LIVE];
static uint32_t lives;

static void check_separate(unsigned char *pointer, uint64_t bytes)
{
    for (uint32_t index = 0; index < lives; ++index)
        CHECK(pointer + bytes <= live[index].Pointer || live[index].Pointer + live[index].Bytes <= pointer);
}

static void release_private(uint32_t index)
{
    for (uint64_t byte = 0; byte < live[index].Bytes; ++byte) CHECK(live[index].Pointer[byte] == live[index].Pattern);
    CHECK(kernel_heap_free(&heap, live[index].Pointer));
    CHECK(!kernel_heap_free(&heap, live[index].Pointer));
    live[index] = live[--lives];
}

// ====== random_cases ======

// 200000 random operations: no overlap, zeroed on allocation, contents intact until free,
// alignment honoured, statistics consistent, double free rejected.
static void random_cases(void)
{
    CHECK(reset_private_fixture(KERNEL_HEAP_MAX_PAGES));

    for (uint32_t round = 0; round < 200000; ++round) {
        if (lives && (lives == LIVE || next_private() % 2)) { release_private(next_private() % lives); continue; }

        uint32_t pick = next_private() % 100;
        uint64_t bytes = pick < 70 ? 1 + next_private() % 2040 : pick < 95 ? 2041 + next_private() % 20000 : 1 + next_private() % 300000;
        uint64_t alignment = 1ULL << (next_private() % 13);
        unsigned char *pointer = kernel_heap_alloc(&heap, bytes, alignment);

        if (!pointer) { CHECK(bytes > 2040 || kernel_heap_stats(&heap).SlabPages == KERNEL_HEAP_SLAB_PAGES || !kernel_heap_stats(&heap).FreePages); continue; }

        CHECK(!((uintptr_t)pointer & (alignment - 1)));
        CHECK(pointer >= arena && pointer + bytes <= arena + sizeof(arena));
        for (uint64_t byte = 0; byte < bytes; ++byte) CHECK(!pointer[byte]);
        check_separate(pointer, bytes);
        live[lives].Pointer = pointer; live[lives].Bytes = bytes; live[lives].Pattern = (unsigned char)(round | 1);
        memset(pointer, live[lives].Pattern, bytes);
        ++lives;

        KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
        CHECK(stats.Objects == lives && stats.FreePages + stats.RunPages + stats.SlabPages == KERNEL_HEAP_MAX_PAGES);
    }

    while (lives) release_private(lives - 1);

    KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
    CHECK(stats.FreePages == KERNEL_HEAP_MAX_PAGES && !stats.RunPages && !stats.SlabPages && !stats.Objects);

    // Contiguous run beyond the legacy 16 MiB (4096 pages) limit.
    unsigned char *large = kernel_heap_pages(&heap, 5000);
    CHECK(large && !((uintptr_t)large & 4095));
    large[0] = 0x5a;
    large[5000ULL * 4096ULL - 1] = 0xa5;
    CHECK(kernel_heap_stats(&heap).RunPages == 5000 && kernel_heap_stats(&heap).FreePages == KERNEL_HEAP_MAX_PAGES - 5000);
    CHECK(kernel_heap_free(&heap, large));
    CHECK(kernel_heap_stats(&heap).FreePages == KERNEL_HEAP_MAX_PAGES && !kernel_heap_stats(&heap).RunPages);
}

// ====== rejected_cases ======

// Invalid arguments, pointers that are not allocations, a corrupted canary, exhaustion.
static void rejected_cases(void)
{
    CHECK(!kernel_heap_init(&heap, arena + 1, 4) && !kernel_heap_init(&heap, arena, 0) &&
        !kernel_heap_init(&heap, arena, KERNEL_HEAP_MAX_PAGES + 1) && !kernel_heap_init(0, arena, 4));
    CHECK(reset_private_fixture(8));
    CHECK(!kernel_heap_alloc(&heap, 0, 0) && !kernel_heap_alloc(&heap, 16, 3) && !kernel_heap_alloc(&heap, 16, 8192));
    CHECK(!kernel_heap_alloc(&heap, 9 * 4096, 0) && !kernel_heap_pages(&heap, 0) && !kernel_heap_pages(&heap, 9));

    unsigned char *small = kernel_heap_alloc(&heap, 24, 0), *page = kernel_heap_pages(&heap, 2);
    CHECK(small && page && !((uintptr_t)page & 4095));
    uint64_t rejected = kernel_heap_stats(&heap).Rejected;
    CHECK(!kernel_heap_free(&heap, small + 1) && !kernel_heap_free(&heap, page + 4096) && !kernel_heap_free(&heap, page + 8));
    CHECK(!kernel_heap_free(&heap, arena + 7 * 4096) && !kernel_heap_free(&heap, arena + 9 * 4096) && !kernel_heap_free(&heap, 0));
    CHECK(kernel_heap_stats(&heap).Rejected == rejected + 6);

    // A write past the requested bytes into the canary is caught at free; the slot stays allocated.
    memset(small, 0x41, 32);
    CHECK(!kernel_heap_free(&heap, small) && kernel_heap_stats(&heap).Objects == 2);

    // Freed memory is poisoned; the last object of a slab gives the page back.
    unsigned char *other = kernel_heap_alloc(&heap, 100, 0);
    CHECK(other && kernel_heap_free(&heap, other) && other[0] == KERNEL_HEAP_POISON && other[127] == KERNEL_HEAP_POISON);
    CHECK(kernel_heap_free(&heap, page) && page[0] == KERNEL_HEAP_POISON && page[8191] == KERNEL_HEAP_POISON);

    // Exhaustion: 8 pages, one held by the small slab with the corrupted object.
    unsigned char *rest = kernel_heap_pages(&heap, 7);
    CHECK(rest && !kernel_heap_pages(&heap, 1) && !kernel_heap_alloc(&heap, 3000, 0));
    CHECK(kernel_heap_stats(&heap).FreePages == 0 && kernel_heap_free(&heap, rest) && kernel_heap_stats(&heap).FreePages == 7);

    // Every class: slots are aligned to the class size and fill exactly one page.
    for (uint32_t klass = 0; klass < KERNEL_HEAP_CLASSES; ++klass) {
        uint32_t size = 16U << klass, slots = 4096 / size;
        unsigned char *first = 0;

        CHECK(reset_private_fixture(4));
        for (uint32_t slot = 0; slot < slots; ++slot) {
            unsigned char *pointer = kernel_heap_alloc(&heap, size - 8, size);
            CHECK(pointer && !((uintptr_t)pointer % size));
            if (!first) first = pointer;
        }
        CHECK(kernel_heap_stats(&heap).SlabPages == 1);
        CHECK(kernel_heap_alloc(&heap, size - 8, size) && kernel_heap_stats(&heap).SlabPages == 2);
        CHECK(!kernel_heap_alloc(&heap, 2041, 0) || klass < KERNEL_HEAP_CLASSES);
        CHECK(kernel_heap_free(&heap, first));
    }
}

// ====== grow_cases ======

static void grow_cases(void)
{
    static KERNEL_HEAP gheap;
    static _Alignas(4096) uint8_t backing[16 * 4096];
    memset(&gheap, 0, sizeof(gheap));
    memset(backing, 0, sizeof(backing));
    CHECK(kernel_heap_init(&gheap, backing, 4));
    void *p1 = kernel_heap_pages(&gheap, 4);
    CHECK(p1 != NULL);
    CHECK(kernel_heap_pages(&gheap, 1) == NULL);
    CHECK(!kernel_heap_grow(&gheap, 0));
    CHECK(!kernel_heap_grow(&gheap, KERNEL_HEAP_MAX_PAGES));
    CHECK(kernel_heap_grow(&gheap, 8));
    CHECK(gheap.Pages == 12 && kernel_heap_stats(&gheap).FreePages == 8);
    void *p2 = kernel_heap_pages(&gheap, 8);
    CHECK(p2 != NULL);
    CHECK(kernel_heap_valid(&gheap));
    CHECK(kernel_heap_free(&gheap, p1));
    CHECK(kernel_heap_free(&gheap, p2));
    CHECK(kernel_heap_stats(&gheap).FreePages == 12 && kernel_heap_valid(&gheap));
}

// ====== main ======

int main(void)
{
    rejected_cases();
    grow_cases();
    random_cases();
    printf("PASS heap: %lu checks; 200000 random operations, grow, oracle, rejections, canary, poison, classes\n", checks);
    return 0;
}

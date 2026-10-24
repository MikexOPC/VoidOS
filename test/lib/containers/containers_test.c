#include "lib/containers/ring.h"
#include "lib/containers/bitmap.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned long checks;
#define CHECK(value) do { ++checks; if (!(value)) { printf("FAIL containers line %d\n", __LINE__); exit(1); } } while (0)

// ====== ring_cases ======

// Fill, drain, wrap of the free-running indices, overflow without loss of stored bytes.
static void ring_cases(void)
{
    static uint8_t storage[32];
    OS_RING ring;
    uint8_t value;

    CHECK(!os_ring_init(&ring, storage, 0) && !os_ring_init(&ring, storage, 24) && !os_ring_init(&ring, 0, 32));
    CHECK(!os_ring_init(&ring, storage, 0x80000001U) && os_ring_init(&ring, storage, 1));
    CHECK(os_ring_push(&ring, 7) && !os_ring_push(&ring, 8) && ring.Overflows == 1 && os_ring_pop(&ring, &value) && value == 7);

    CHECK(os_ring_init(&ring, storage, 32) && !os_ring_pop(&ring, &value) && !os_ring_count(&ring));
    for (uint32_t index = 0; index < 32; ++index) CHECK(os_ring_push(&ring, (uint8_t)index));
    CHECK(!os_ring_push(&ring, 99) && ring.Overflows == 1 && os_ring_count(&ring) == 32);
    for (uint32_t index = 0; index < 32; ++index) CHECK(os_ring_pop(&ring, &value) && value == (uint8_t)index);
    CHECK(!os_ring_pop(&ring, &value) && !os_ring_pop(&ring, 0));

    // Indices near 2^32: the count and the slots stay right across the wrap.
    ring.Head = ring.Tail = UINT32_MAX - 5;
    for (uint32_t round = 0; round < 1000; ++round) {
        uint32_t fill = round % 33;

        for (uint32_t index = 0; index < fill; ++index) CHECK(os_ring_push(&ring, (uint8_t)(round + index)));
        CHECK(os_ring_count(&ring) == fill);
        for (uint32_t index = 0; index < fill; ++index) CHECK(os_ring_pop(&ring, &value) && value == (uint8_t)(round + index));
    }

    ring.Overflows = UINT32_MAX;
    for (uint32_t index = 0; index < 32; ++index) CHECK(os_ring_push(&ring, 1));
    CHECK(!os_ring_push(&ring, 1) && ring.Overflows == UINT32_MAX);
    ring.Head = ring.Tail + 33;
    CHECK(!os_ring_push(&ring, 1) && !os_ring_pop(&ring, &value) && !os_ring_count(&ring));
}

// ====== bitmap_cases ======

// Every set bit is found exactly once in order, across word boundaries and the last partial word.
static void bitmap_cases(void)
{
    static uint64_t words[5];
    OS_BITMAP bitmap;
    uint32_t index;

    CHECK(!os_bitmap_init(&bitmap, words, 0) && !os_bitmap_init(&bitmap, 0, 8) && !os_bitmap_init(0, words, 8));

    for (uint32_t bits = 1; bits <= 300; ++bits) {
        words[4] = 0xffffffffffffffffULL;
        CHECK(os_bitmap_init(&bitmap, words, bits));
        if (bits <= 256) CHECK(words[4] == 0xffffffffffffffffULL);
        CHECK(os_bitmap_next(&bitmap, 0, &index) == 0);
        CHECK(!os_bitmap_set(&bitmap, bits) && !os_bitmap_test(&bitmap, bits) && os_bitmap_next(&bitmap, bits + 1, &index) == -1);

        for (uint32_t bit = 0; bit < bits; bit += 1 + bit % 7) CHECK(os_bitmap_set(&bitmap, bit));

        uint32_t start = 0, found = 0;
        int result;

        while ((result = os_bitmap_next(&bitmap, start, &index)) == 1) {
            CHECK(index >= start && index < bits && os_bitmap_test(&bitmap, index));
            for (uint32_t gap = start; gap < index; ++gap) CHECK(!os_bitmap_test(&bitmap, gap));
            CHECK(os_bitmap_clear(&bitmap, index) && !os_bitmap_test(&bitmap, index));
            start = index + 1;
            ++found;
        }

        CHECK(result == 0 && found && os_bitmap_next(&bitmap, 0, &index) == 0);
    }

    CHECK(os_bitmap_init(&bitmap, words, 128) && os_bitmap_set(&bitmap, 63) && os_bitmap_set(&bitmap, 64));
    CHECK(os_bitmap_next(&bitmap, 63, &index) == 1 && index == 63 && os_bitmap_next(&bitmap, 64, &index) == 1 && index == 64);
    CHECK(os_bitmap_next(&bitmap, 65, &index) == 0 && os_bitmap_next(&bitmap, 128, &index) == 0 && os_bitmap_next(0, 0, &index) == -1);
}

// ====== main ======

int main(void)
{
    ring_cases();
    bitmap_cases();
    printf("PASS containers: %lu checks; ring fill/drain/wrap/overflow, bitmap 1..300 bits\n", checks);
    return 0;
}

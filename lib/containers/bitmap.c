#include "bitmap.h"

// ====== os_bitmap_init ======

// Every word of the storage is cleared, including the unused bits of the last one.
int os_bitmap_init(OS_BITMAP *bitmap, uint64_t *words, uint32_t bits)
{
    if (!bitmap || !words || !bits) return 0;

    for (uint32_t word = 0; word < (bits + 63ULL) / 64; ++word) words[word] = 0;

    *bitmap = (OS_BITMAP){words, bits};
    return 1;
}

// ====== os_bitmap_set ======

int os_bitmap_set(OS_BITMAP *bitmap, uint32_t index)
{
    if (!bitmap || !bitmap->Words || index >= bitmap->Bits) return 0;

    bitmap->Words[index / 64] |= 1ULL << (index % 64);
    return 1;
}

// ====== os_bitmap_clear ======

int os_bitmap_clear(OS_BITMAP *bitmap, uint32_t index)
{
    if (!bitmap || !bitmap->Words || index >= bitmap->Bits) return 0;

    bitmap->Words[index / 64] &= ~(1ULL << (index % 64));
    return 1;
}

// ====== os_bitmap_test ======

int os_bitmap_test(const OS_BITMAP *bitmap, uint32_t index)
{
    return bitmap && bitmap->Words && index < bitmap->Bits && ((bitmap->Words[index / 64] >> (index % 64)) & 1);
}

// ====== os_bitmap_next ======

// Word-wise scan: an empty word costs one test for 64 bits.
int os_bitmap_next(const OS_BITMAP *bitmap, uint32_t start, uint32_t *index)
{
    if (!bitmap || !bitmap->Words || !index || start > bitmap->Bits) return -1;

    for (uint32_t current = start; current < bitmap->Bits; current = (current / 64 + 1) * 64) {
        uint64_t pending = bitmap->Words[current / 64] >> (current % 64);

        if (!pending) continue;

        while (!(pending & 1)) { pending >>= 1; ++current; }

        if (current >= bitmap->Bits) return 0;

        *index = current;
        return 1;
    }

    return 0;
}

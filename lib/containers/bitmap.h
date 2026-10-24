#ifndef OS_LIB_CONTAINERS_BITMAP_H
#define OS_LIB_CONTAINERS_BITMAP_H

#include <stdint.h>

// G1: bit set over caller storage of (Bits + 63) / 64 words, bit i in word i / 64 at i % 64.
typedef struct {
    uint64_t  *Words;
    uint32_t  Bits;
} OS_BITMAP;

int  os_bitmap_init(OS_BITMAP *bitmap, uint64_t *words, uint32_t bits);
int  os_bitmap_set(OS_BITMAP *bitmap, uint32_t index);
int  os_bitmap_clear(OS_BITMAP *bitmap, uint32_t index);
int  os_bitmap_test(const OS_BITMAP *bitmap, uint32_t index);
// 1 and *index of the first set bit at or after start, 0 when none, -1 on invalid arguments.
int  os_bitmap_next(const OS_BITMAP *bitmap, uint32_t start, uint32_t *index);

#endif

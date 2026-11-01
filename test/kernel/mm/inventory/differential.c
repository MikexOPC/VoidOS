#include <stdio.h>
#include <string.h>
#include "kernel/mm/inventory.h"
int reference_inventory(const unsigned char *, BOOT_U64, BOOT_U64, KERNEL_MEMORY_INVENTORY *);
int run_alias_differential(void);
int run_physical_differential(void);
static unsigned char map[4096 * 48];
static unsigned long cases;

// ====== put_number ======

static void put_number(unsigned char *to, BOOT_U64 value)
{
    for (unsigned index = 0; index < 8; ++index) to[index] = (unsigned char)(value >> (index * 8));
}

// ====== descriptor ======

static void descriptor(unsigned index, BOOT_U64 base, BOOT_U64 pages)
{
    unsigned char *row = map + index * 48;
    memset(row, 0, 48); row[0] = 7; row[32] = 8;
    put_number(row + 8, base); put_number(row + 24, pages);
}

// ====== compare ======

static int compare(BOOT_U64 bytes, BOOT_U64 stride)
{
    KERNEL_MEMORY_INVENTORY actual, expected;
    memset(&actual, 0xa5, sizeof(actual)); memset(&expected, 0xa5, sizeof(expected));
    int a = kernel_inventory_memory(map, bytes, stride, &actual);
    int b = reference_inventory(map, bytes, stride, &expected);
    ++cases;
    return a == b && actual.ConventionalPages == expected.ConventionalPages &&
        actual.ReservedPages == expected.ReservedPages && actual.Regions == expected.Regions;
}

// ====== ordered_and_unordered ======

static int ordered_and_unordered(void)
{
    for (unsigned count = 1; count <= 4096; count *= 2) {
        for (unsigned i = 0; i < count; ++i) descriptor(i, (BOOT_U64)i * 8192, 1);
        if (!compare((BOOT_U64)count * 48, 48)) return 1;
        for (unsigned i = 0; i < count; ++i) descriptor(i, (BOOT_U64)(count - i - 1) * 8192, 1);
        if (!compare((BOOT_U64)count * 48, 48)) return 2;
    }
    unsigned state = 0x516837;
    for (unsigned trial = 0; trial < 1500; ++trial) {
        unsigned count = 2 + trial % 127;
        for (unsigned i = 0; i < count; ++i) descriptor(i, (BOOT_U64)i * 16384, 2);
        for (unsigned i = count - 1; i; --i) {
            unsigned char temporary[48];
            state = state * 1664525u + 1013904223u;
            unsigned other = state % (i + 1);
            memcpy(temporary, map + i * 48, 48);
            memcpy(map + i * 48, map + other * 48, 48);
            memcpy(map + other * 48, temporary, 48);
        }
        if (!compare((BOOT_U64)count * 48, 48)) return 3;
        if (trial & 1) memcpy(map + (count - 1) * 48, map, 48);
        else put_number(map + (count - 1) * 48 + 8, 1);
        if (!compare((BOOT_U64)count * 48, 48)) return 4;
    }
    return 0;
}

// ====== boundaries ======

static int boundaries(void)
{
    const BOOT_U64 bases[] = {0, 1, 4096, (1ULL << 52) - 4096, 1ULL << 52, ~0ULL - 4095};
    const BOOT_U64 pages[] = {0, 1, 2, 1ULL << 40, ~0ULL};
    for (unsigned a = 0; a < sizeof(bases) / sizeof(*bases); ++a)
        for (unsigned b = 0; b < sizeof(pages) / sizeof(*pages); ++b) {
            descriptor(0, bases[a], pages[b]);
            if (!compare(48, 48)) return 1;
        }
    descriptor(0, 0x10000, 8); descriptor(1, 0x30000, 1);
    descriptor(2, 0x11000, 1);
    if (!compare(144, 48)) return 2;
    descriptor(2, 0, 1);
    if (!compare(144, 48)) return 3;
    descriptor(2, 0x18000, 1);
    if (!compare(144, 48)) return 4;
    return compare(0, 48) && compare(143, 48) && compare(144, 39) && compare(144, 4097) ? 0 : 5;
}

// ====== main ======

int main(void)
{
    int result = ordered_and_unordered();
    if (!result) result = boundaries();
    if (result) { printf("FAIL memory differential %d\n", result); return 1; }
    printf("PASS memory differential: %lu cases\n", cases);
    result = run_physical_differential();
    if (result) { printf("FAIL physical differential %d\n", result); return 1; }
    result = run_alias_differential();
    if (result) { printf("FAIL alias differential %d\n", result); return 1; }
    return 0;
}

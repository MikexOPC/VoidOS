#include "lib/memory/address_ranges.h"

// ====== casi limite degli intervalli e dell'indirizzo massimo ====== //

typedef struct {
    uint64_t First;
    uint64_t FirstBytes;
    uint64_t Second;
    uint64_t SecondBytes;
    int Expected;
} RANGE_CASE;

static int check_boundaries(void)
{
    static const RANGE_CASE cases[] = {
        {0, 0, 0, 0, 0},
        {0, 0, 0, 1, 0},
        {0, 1, 0, 0, 0},
        {0, 1, 0, 1, 1},
        {0, 4096, 4096, 4096, 0},
        {0, 4096, 4095, 4096, 1},
        {4096, 4096, 0, 8192, 1},
        {4096, 4096, 4097, 1, 1},
        {0, UINT64_MAX, UINT64_MAX, 1, 0},
        {0, UINT64_MAX, UINT64_MAX - 1, 1, 1},
        {UINT64_MAX, 1, UINT64_MAX, 1, 1},
        {UINT64_MAX - 1, 1, UINT64_MAX, 1, 0},
        {UINT64_MAX - 1, 2, UINT64_MAX, 1, 1},
        {UINT64_MAX, 2, 0, 1, 0},
        {UINT64_MAX - 1, UINT64_MAX, UINT64_MAX, 1, 1},
        {0, 1, UINT64_MAX, UINT64_MAX, 0}
    };
    unsigned int index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const RANGE_CASE *test = &cases[index];
        if (os_address_ranges_overlap(test->First, test->FirstBytes, test->Second, test->SecondBytes) != test->Expected ||
            os_address_ranges_overlap(test->Second, test->SecondBytes, test->First, test->FirstBytes) != test->Expected)
            return 1 + (int)index;
    }
    return 0;
}

// ====== oracolo indipendente tramite enumerazione dei singoli indirizzi ====== //

static int enumerated_overlap(uint64_t first, uint64_t first_bytes,
                              uint64_t second, uint64_t second_bytes)
{
    uint64_t address;
    for (address = 0; address < 32; ++address)
        if (address >= first && address < first + first_bytes &&
            address >= second && address < second + second_bytes)
            return 1;
    return 0;
}

static int check_small_ranges(void)
{
    uint64_t first, first_bytes, second, second_bytes;
    for (first = 0; first < 16; ++first)
        for (first_bytes = 0; first_bytes <= 16; ++first_bytes)
            for (second = 0; second < 16; ++second)
                for (second_bytes = 0; second_bytes <= 16; ++second_bytes)
                    if (os_address_ranges_overlap(first, first_bytes, second, second_bytes) !=
                        enumerated_overlap(first, first_bytes, second, second_bytes))
                        return 100;
    return 0;
}

// ====== ingresso host senza dipendenza da tipi o servizi UEFI ====== //

__declspec(dllexport) int run_address_ranges_tests(void)
{
    int result = check_boundaries();
    return result ? result : check_small_ranges();
}

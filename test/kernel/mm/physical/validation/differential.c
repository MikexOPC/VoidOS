#include <stdio.h>
#include <string.h>
#include "kernel/mm/physical/internal.h"
int reference_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner);
static KERNEL_PHYSICAL_ALLOCATOR model, pristine;

// ====== seed ======

static void seed(unsigned free_count, unsigned live_count)
{
    memset(&model, 0, sizeof(model));
    model.Self = (BOOT_U64)&model; model.Ready = 1; model.PhysicalBits = 40;
    model.Count = free_count; model.NextId = 65;
    model.FreePages = free_count; model.TotalPages = free_count + live_count;
    for (unsigned i = 0; i < free_count; ++i)
        model.Free[i] = (KERNEL_PHYSICAL_EXTENT){0x1000 + (BOOT_U64)i * 8192, 1};
    for (unsigned i = 0; i < live_count; ++i)
        model.Live[i] = (KERNEL_PHYSICAL_LEASE){0x1000000 + (BOOT_U64)(live_count - i) * 8192,
            1, i + 1, (BOOT_U64)&model};
}

// ====== same ======

static int same(void)
{
    return kphys_validate(&model) == reference_physical_validate(&model);
}

// ====== run_physical_differential ======

int run_physical_differential(void)
{
    unsigned cases = 0;
    if (kphys_validate(0) || reference_physical_validate(0)) return 1;
    for (unsigned free_count = 0; free_count <= 256; free_count += 16)
        for (unsigned live_count = 0; live_count <= 64; ++live_count) {
            seed(free_count, live_count);
            if (!same() || (free_count + live_count && !kphys_validate(&model))) return 2;
            ++cases;
        }
    seed(16, 64); pristine = model;
    for (unsigned offset = 0; offset < sizeof(model); ++offset) {
        model = pristine;
        ((unsigned char *)&model)[offset] ^= (unsigned char)(1U << (offset % 8));
        if (!same()) return 3;
        ++cases;
    }
    for (unsigned i = 0; i < 64; ++i) {
        model = pristine; model.Live[i].Base = model.Free[i % 16].Base;
        if (!same() || kphys_validate(&model)) return 4;
        model = pristine; model.Live[i].Base = ~0ULL - 4095;
        if (!same() || kphys_validate(&model)) return 5;
        model = pristine; model.Live[i].Pages = ~0ULL;
        if (!same() || kphys_validate(&model)) return 6;
        if (i) {
            model = pristine; model.Live[i].Base = model.Live[0].Base;
            if (!same() || kphys_validate(&model)) return 7;
            model = pristine; model.Live[i].Id = model.Live[0].Id;
            if (!same() || kphys_validate(&model)) return 8;
            cases += 2;
        }
        cases += 3;
    }
    printf("PASS physical validation differential: %u cases\n", cases);
    return 0;
}

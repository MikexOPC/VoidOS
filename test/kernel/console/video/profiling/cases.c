#include "profile.h"
#include <assert.h>
#include <stdio.h>

// ====== kernel_console_pm_limit ======

uint32_t kernel_console_pm_limit(void) { return 357955; }

// ====== sample ======

static int sample(KERNEL_CONSOLE_PROFILE *owner, uint32_t base, uint32_t step)
{
    uint32_t phase;
    for (phase = 0; phase <= KERNEL_CONSOLE_PROFILE_PHASES; ++phase)
        if (!kernel_console_profile_mark(owner, phase, (base + phase * step) & owner->Mask)) return 0;
    return 1;
}

// ====== main ======

int main(void)
{
    KERNEL_CONSOLE_PROFILE owner, copy;
    _Alignas(KERNEL_CONSOLE_PROFILE) unsigned char bytes[sizeof(KERNEL_CONSOLE_PROFILE) + 1];
    uint32_t mask, phase;
    assert(!kernel_console_profile_init(NULL, 0xffffff));
    assert(!kernel_console_profile_mark(NULL, 0, 0));
    assert(!kernel_console_profile_init((KERNEL_CONSOLE_PROFILE *)(bytes + 1), 0xffffff));
    assert(!kernel_console_profile_mark((KERNEL_CONSOLE_PROFILE *)(bytes + 1), 0, 0));
    assert(!kernel_console_profile_init(&owner, 0xffff));
    for (mask = 0; mask < 2; ++mask) {
        assert(kernel_console_profile_init(&owner, mask ? UINT32_MAX : 0xffffff));
        assert(sample(&owner, owner.Mask - 2, 1));
        assert(owner.Cells == 1 && owner.Total == 6 && !owner.Next);
        assert(sample(&owner, 0, 100));
        for (phase = 0; phase < 6; ++phase) assert(owner.Sum[phase] == 101 && owner.Max[phase] == 100);
        copy = owner;
        assert(!kernel_console_profile_mark(&copy, 0, 0));
        assert(!kernel_console_profile_mark(&owner, 1, 0));
        assert(owner.Failed && !kernel_console_profile_mark(&owner, 0, 0));
        assert(kernel_console_profile_init(&owner, mask ? UINT32_MAX : 0xffffff));
        assert(!sample(&owner, 0, 0) && owner.Cells == 0 && owner.Total == 0);
        assert(kernel_console_profile_init(&owner, mask ? UINT32_MAX : 0xffffff));
        assert(!sample(&owner, 0, 60000) && owner.Cells == 0);
        assert(kernel_console_profile_init(&owner, mask ? UINT32_MAX : 0xffffff));
        owner.Sum[5] = UINT64_MAX;
        assert(!sample(&owner, 0, 1) && owner.Sum[0] == 0 && owner.Total == 0);
        assert(kernel_console_profile_init(&owner, mask ? UINT32_MAX : 0xffffff));
        owner.Cells = UINT64_MAX;
        assert(!sample(&owner, 0, 1));
        assert(kernel_console_profile_init(&owner, mask ? UINT32_MAX : 0xffffff));
        owner.Total = UINT64_MAX;
        assert(!sample(&owner, 0, 1));
    }
    assert(kernel_console_profile_init(&owner, 0xffffff));
    assert(!kernel_console_profile_mark(&owner, 0, 0x1000000));
    assert(kernel_console_profile_init(&owner, 0xffffff));
    assert(kernel_console_profile_mark(&owner, 0, 10));
    assert(kernel_console_profile_mark(&owner, 1, 9));
    for (phase = 2; phase < 6; ++phase) assert(kernel_console_profile_mark(&owner, phase, 10));
    assert(!kernel_console_profile_mark(&owner, 6, 11) && owner.Cells == 0);
    assert(kernel_console_profile_init(&owner, 0xffffff));
    for (phase = 0; phase < 6; ++phase) assert(kernel_console_profile_mark(&owner, phase, 0));
    assert(kernel_console_profile_mark(&owner, 6, 1) && owner.Sum[5] == 1);
    assert(kernel_console_profile_init(&owner, 0xffffff));
    for (phase = 0; phase < 6; ++phase) assert(kernel_console_profile_mark(&owner, phase, 0));
    assert(kernel_console_profile_mark(&owner, 6, 357955) && owner.Total == 357955);
    assert(kernel_console_profile_init(&owner, 0xffffff));
    for (phase = 0; phase < 6; ++phase) assert(kernel_console_profile_mark(&owner, phase, 0));
    assert(!kernel_console_profile_mark(&owner, 6, 357956) && owner.Total == 0);
    puts("PASS: ordered phase profiling, wrap, partial samples and overflow rollback");
    return 0;
}

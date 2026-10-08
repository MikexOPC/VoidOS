#include "../mock.h"
#include <string.h>

// ====== narrow_candidates ======

static void narrow_candidates(void)
{
    test_hpet[0x104/4] = 1u << 16;
    test_hpet[0x124/4] = 1u << 17;
    test_hpet[0x144/4] = 1u << 23;
}

// ====== round_trip ======

/* The RAM device model validates selection, bounds and register ownership;
 * it does not measure real hardware ordering or interrupt latency. */
static int round_trip(BOOT_U32 timer, BOOT_U32 pin)
{
    BOOT_U32 hpet[1024], io[256];
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    memcpy(hpet,test_hpet,sizeof(hpet)); memcpy(io,test_io,sizeof(io));
    if (!x86_watchdog_open(&test_env) || owner->Timer != timer || owner->Pin != pin ||
        x86_watchdog_device(6) || x86_watchdog_select(0) ||
        !x86_watchdog_wait(test_source,1,NULL) || owner->Count != 1 || test_in_irq ||
        !x86_watchdog_close() || !owner->Closed || owner->Acquired || owner->RouteOwned ||
        owner->Token.Id || owner->Armed || test_env.Window->Active ||
        test_env.Window->Pool->FreePages != 42) { return 0; }
    return !memcmp(hpet,test_hpet,sizeof(hpet)) && !memcmp(io,test_io,sizeof(io));
}

// ====== rejected_without_fallback ======

static int rejected_without_fallback(BOOT_U32 mode)
{
    BOOT_U32 hpet[1024], io[256];
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    watchdog_test_reset(mode); narrow_candidates();
    if (mode == 25) { test_io[0x30] = 0x40; }
    memcpy(hpet,test_hpet,sizeof(hpet)); memcpy(io,test_io,sizeof(io));
    return !x86_watchdog_open(&test_env) && owner->Timer == 0 && owner->Closed &&
        !owner->Acquired && !owner->RouteOwned && !owner->Token.Id &&
        !test_env.Window->Active && test_env.Window->Pool->FreePages == 42 &&
        !memcmp(hpet,test_hpet,sizeof(hpet)) && !memcmp(io,test_io,sizeof(io));
}

// ====== run_watchdog_compatibility_tests ======

int run_watchdog_compatibility_tests(void)
{
    BOOT_U32 timer;
    KERNEL_MADT_TOPOLOGY *topology;
    X86_HPET_DESCRIPTION *description;
    watchdog_test_reset(0); narrow_candidates();
    test_io[0x30] = 0x40;
    if (!round_trip(1,17)) { return 1; }
    watchdog_test_reset(0); narrow_candidates();
    test_hpet[0x104/4] = 0xffff; /* ISA routes are outside this profile. */
    if (!round_trip(1,17)) { return 2; }
    watchdog_test_reset(0); narrow_candidates();
    topology = (KERNEL_MADT_TOPOLOGY *)test_env.Topology;
    topology->IsoCount = 1; topology->Iso[0].Gsi = 16;
    topology->NmiCount = 1; topology->Nmi[0].Kind = 3; topology->Nmi[0].Value = 17;
    if (!round_trip(2,23)) { return 3; }
    watchdog_test_reset(9);
    if (x86_watchdog_open(&test_env) || !x86_watchdog_state()->Closed ||
        x86_watchdog_state()->Timer != 2 || test_env.Window->Active) { return 4; }
    watchdog_test_reset(0); narrow_candidates();
    test_hpet[0x140/4] |= 4; /* A busy comparator invalidates whole-block ownership. */
    if (x86_watchdog_open(&test_env) || x86_watchdog_state()->Acquired ||
        test_hpet[0x100/4] != 0x30 || test_io[0x30] != 0x10040) { return 5; }
    if (!rejected_without_fallback(24)) { return 6; }
    if (!rejected_without_fallback(18)) { return 7; }
    if (!rejected_without_fallback(19)) { return 8; }
    if (!rejected_without_fallback(25)) { return 9; }
    watchdog_test_reset(0);
    description = (X86_HPET_DESCRIPTION *)test_env.Description;
    description->BlockId = test_hpet[0] = 0x8086bf01;
    for (timer = 0; timer < 32; ++timer) {
        BOOT_U32 offset = 0x100 + timer * 32;
        test_hpet[offset/4] = 0x30;
        test_hpet[(offset+4)/4] = timer == 31 ? 1u << 16 : 0;
        test_hpet[(offset+8)/4] = 0x12340000 + timer;
        test_hpet[(offset+12)/4] = 0x43210000 + timer;
    }
    if (!round_trip(31,16)) { return 10; }
    return 0;
}

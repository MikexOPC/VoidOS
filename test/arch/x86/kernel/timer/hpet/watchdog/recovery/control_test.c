#include "../mock.h"

// ====== prepare_failed_restore ======

static int prepare_failed_restore(void)
{
    watchdog_test_reset(15);
    return x86_watchdog_open(&test_env) && !x86_watchdog_close() &&
        x86_watchdog_cleanup_failure() == X86_WATCHDOG_RESTORE_DEVICE &&
        x86_watchdog_recovery_status() == X86_WATCHDOG_RETRY_CANDIDATE &&
        !x86_watchdog_recovery_attempts();
}

// ====== released_resources ======

static int released_resources(void)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    return owner->Closed && !owner->Armed && !owner->Error && !owner->Acquired &&
        !owner->RouteOwned && !owner->Token.Id && !test_env.Window->Active &&
        test_env.Window->Pool->FreePages == 42 && test_hpet[0x10/4] == 0 &&
        test_hpet[0xf0/4] == 1234 && test_hpet[0xf4/4] == 4 &&
        test_io[0x30] == 0x10040 && !test_io[0x31];
}

// ====== run_watchdog_recovery_tests ======

int run_watchdog_recovery_tests(void)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    BOOT_U32 borrows, attempt;
    watchdog_test_reset(0);
    if (x86_watchdog_open(NULL) || x86_watchdog_recover() != X86_WATCHDOG_REJECTED ||
        x86_watchdog_recovery_attempts()) { return 1; }
    if (!x86_watchdog_open(&test_env)) { return 2; }
    borrows = test_borrows;
    if (x86_watchdog_recover() != X86_WATCHDOG_RUNNING || test_borrows != borrows) { return 3; }
    if (x86_watchdog_wait(NULL,1,NULL) ||
        x86_watchdog_recover() != X86_WATCHDOG_STOP_REQUIRED || test_borrows != borrows ||
        !x86_watchdog_close() || x86_watchdog_recover() != X86_WATCHDOG_RELEASED) { return 4; }
    /* Released query must not dereference Env after its lifetime ends. */
    owner->Env.Window = NULL; owner->Env.Mapping = NULL;
    if (x86_watchdog_recover() != X86_WATCHDOG_RELEASED) { return 5; }
    if (!prepare_failed_restore()) { return 6; }
    test_mode = 0;
    if (x86_watchdog_recover() != X86_WATCHDOG_RELEASED || !released_resources() ||
        x86_watchdog_recovery_attempts() != 1 ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_RESTORE_DEVICE) { return 7; }
    if (!prepare_failed_restore()) { return 8; }
    for (attempt = 1; attempt <= 3; ++attempt) {
        BOOT_U32 expected = attempt == 3 ? X86_WATCHDOG_EXHAUSTED : X86_WATCHDOG_RETAIN;
        if (x86_watchdog_recover() != expected || x86_watchdog_recovery_attempts() != attempt ||
            owner->Closed || !owner->Acquired || !owner->RouteOwned) { return 9; }
    }
    borrows = test_borrows; test_mode = 0;
    if (x86_watchdog_recover() != X86_WATCHDOG_EXHAUSTED || test_borrows != borrows) { return 10; }
    if (!prepare_failed_restore()) { return 11; }
    test_mode = 0; test_hpet[0x20/4] = 1;
    if (x86_watchdog_recover() != X86_WATCHDOG_RETAIN || owner->Closed || !owner->Acquired ||
        test_hpet[0x20/4] != 1 || x86_watchdog_recovery_attempts() != 1) { return 12; }
    test_hpet[0x20/4] = 0; test_io[0x30] |= 0x4000;
    if (x86_watchdog_recover() != X86_WATCHDOG_RETAIN || owner->Closed || !owner->RouteOwned) { return 13; }
    test_io[0x30] &= ~0x4000u;
    if (x86_watchdog_recover() != X86_WATCHDOG_RELEASED || !released_resources()) { return 14; }
    watchdog_test_reset(21);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(test_source,1,NULL) ||
        x86_watchdog_close()) { return 15; }
    borrows = test_borrows;
    if (x86_watchdog_recover() != X86_WATCHDOG_RETAIN || test_borrows != borrows ||
        x86_watchdog_recovery_attempts() || !owner->Token.Id || !owner->Acquired) { return 16; }
    watchdog_test_reset(2);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(test_source,1,NULL) ||
        x86_watchdog_recover() != X86_WATCHDOG_STOP_REQUIRED || !x86_watchdog_close() ||
        x86_watchdog_recover() != X86_WATCHDOG_RELEASED || x86_watchdog_failure() != X86_WATCHDOG_DEADLINE) {
        return 17;
    }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) { return 18; }
    owner->Armed = 1; borrows = test_borrows;
    if (x86_watchdog_recover() != X86_WATCHDOG_RETAIN || test_borrows != borrows ||
        x86_watchdog_recovery_attempts()) { return 18; }
    owner->Armed = 0; owner->Ready = 2;
    if (x86_watchdog_recover() != X86_WATCHDOG_RETAIN || test_borrows != borrows) { return 18; }
    owner->Ready = 1;
    if (!x86_watchdog_close()) { return 18; }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) { return 19; }
    test_mode = 28;
    if (x86_watchdog_close() || owner->Acquired || !owner->RouteOwned || !owner->Ready ||
        test_io[0x30] != owner->IoLow || x86_watchdog_cleanup_failure() != X86_WATCHDOG_RESTORE_ROUTE) {
        return 19;
    }
    test_mode = 0;
    if (!x86_watchdog_close() || !released_resources() || x86_watchdog_recovery_attempts() != 1) { return 20; }
    if (!prepare_failed_restore()) { return 21; }
    for (attempt = 0; attempt < 3; ++attempt) { if (x86_watchdog_close()) { return 21; } }
    borrows = test_borrows; test_mode = 0;
    if (x86_watchdog_close() || test_borrows != borrows || x86_watchdog_recovery_attempts() != 3 ||
        !owner->Acquired || !owner->RouteOwned) { return 21; }
    if (!prepare_failed_restore()) { return 22; }
    test_mode = 0; test_hpet[0] ^= 1;
    if (x86_watchdog_recover() != X86_WATCHDOG_RETAIN || owner->Closed || !owner->Acquired) { return 22; }
    test_hpet[0] ^= 1;
    if (x86_watchdog_recover() != X86_WATCHDOG_RELEASED || !released_resources()) { return 22; }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) { return 23; }
    owner->Count = ~0u;
    if (x86_watchdog_recovery_status() != X86_WATCHDOG_STOP_REQUIRED ||
        x86_watchdog_failure() || !x86_watchdog_close()) { return 23; }
    return 0;
}

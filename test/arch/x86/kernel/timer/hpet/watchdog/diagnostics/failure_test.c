#include "../mock.h"

// ====== expected_open_failure ======

static int expected_open_failure(BOOT_U32 mode, BOOT_U32 phase)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    watchdog_test_reset(mode);
    return !x86_watchdog_open(&test_env) && x86_watchdog_failure() == phase &&
        !x86_watchdog_cleanup_failure() && !owner->Acquired && !owner->RouteOwned &&
        !owner->Token.Id && !test_env.Window->Active && test_env.Window->Pool->FreePages == 42;
}

// ====== expected_wait_failure ======

static int expected_wait_failure(BOOT_U32 mode, BOOT_U32 phase, BOOT_U32 cleanup)
{
    watchdog_test_reset(mode);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(test_source,1,NULL) ||
        x86_watchdog_failure() != phase) { return 0; }
    if ((x86_watchdog_close() != 0) != (cleanup == 0)) { return 0; }
    return x86_watchdog_failure() == phase && x86_watchdog_cleanup_failure() == cleanup &&
        !test_env.Window->Active && test_env.Window->Pool->FreePages == 42;
}

// ====== run_watchdog_diagnostics_tests ======

int run_watchdog_diagnostics_tests(void)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    watchdog_test_reset(0);
    if (x86_watchdog_open(NULL) || x86_watchdog_failure() != X86_WATCHDOG_ENVIRONMENT ||
        x86_watchdog_cleanup_failure()) { return 1; }
    if (!x86_watchdog_open(&test_env) || x86_watchdog_failure() ||
        x86_watchdog_cleanup_failure()) { return 2; }
    x86_watchdog_fail(0); x86_watchdog_fail(~0u);
    if (x86_watchdog_failure() || !x86_watchdog_wait(test_source,1,NULL) ||
        x86_watchdog_failure() || !x86_watchdog_close()) { return 3; }
    if (!expected_open_failure(10,X86_WATCHDOG_PROFILE)) { return 4; }
    if (!expected_open_failure(4,X86_WATCHDOG_CAPTURE)) { return 5; }
    if (!expected_open_failure(7,X86_WATCHDOG_CAPTURE)) { return 6; }
    if (!expected_open_failure(9,X86_WATCHDOG_SELECTION)) { return 7; }
    if (!expected_open_failure(16,X86_WATCHDOG_BIND)) { return 8; }
    if (!expected_open_failure(18,X86_WATCHDOG_ROUTE)) { return 9; }
    if (!expected_open_failure(20,X86_WATCHDOG_CONFIGURE)) { return 10; }
    if (!expected_open_failure(24,X86_WATCHDOG_ROUTE)) { return 11; }
    if (!expected_wait_failure(14,X86_WATCHDOG_ARM,0) || x86_watchdog_arm_detail() != 9) { return 12; }
    if (!expected_wait_failure(2,X86_WATCHDOG_DEADLINE,0)) { return 13; }
    if (!expected_wait_failure(21,X86_WATCHDOG_IRQ,X86_WATCHDOG_CLOSE_STATE)) { return 14; }
    if (!expected_wait_failure(22,X86_WATCHDOG_IRQ,X86_WATCHDOG_CLOSE_STATE)) { return 15; }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(NULL,1,NULL) ||
        x86_watchdog_failure() != X86_WATCHDOG_SOURCE) { return 16; }
    test_mode = 15;
    if (x86_watchdog_close() || x86_watchdog_failure() != X86_WATCHDOG_SOURCE ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_RESTORE_DEVICE || !owner->Acquired) { return 17; }
    test_mode = 0;
    if (!x86_watchdog_close() || !owner->Closed || owner->Token.Id || owner->Acquired ||
        owner->RouteOwned || x86_watchdog_failure() != X86_WATCHDOG_SOURCE ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_RESTORE_DEVICE) { return 18; }
    watchdog_test_reset(26);
    if (x86_watchdog_open(&test_env) || x86_watchdog_failure() != X86_WATCHDOG_BIND ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_RESTORE_DEVICE || owner->Closed ||
        !owner->Acquired || !owner->RouteOwned || owner->Token.Id) { return 19; }
    test_mode = 0;
    if (!x86_watchdog_close() || !owner->Closed || owner->Acquired || owner->RouteOwned ||
        x86_watchdog_failure() != X86_WATCHDOG_BIND ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_RESTORE_DEVICE) { return 20; }
    watchdog_test_reset(17);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_close() || x86_watchdog_failure() ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_UNBIND || !owner->Token.Id) { return 21; }
    test_mode = 0;
    if (!x86_watchdog_close() || !owner->Closed ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_UNBIND) { return 22; }
    watchdog_test_reset(27);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_close() || x86_watchdog_failure() ||
        x86_watchdog_cleanup_failure() != X86_WATCHDOG_LOG || !owner->Closed ||
        owner->Acquired || owner->RouteOwned || owner->Token.Id) { return 23; }
    watchdog_test_reset(23);
    if (!x86_watchdog_open(&test_env) || !x86_watchdog_wait(test_source,1,&test_stop) ||
        !x86_watchdog_close() || x86_watchdog_failure() || x86_watchdog_cleanup_failure()) { return 24; }
    for (BOOT_U32 mode = 31; mode <= 33; ++mode) {
        BOOT_U32 expected = mode == 31 ? 5u : mode == 32 ? 11u : 7u;
        if (!expected_wait_failure(mode,X86_WATCHDOG_ARM,0) ||
            x86_watchdog_arm_detail() != expected) { return (int)mode; }
    }
    return 0;
}

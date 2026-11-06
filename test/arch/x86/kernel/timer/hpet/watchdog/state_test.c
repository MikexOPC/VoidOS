#include "mock.h"

// ====== restored ======

static int restored(void)
{
    X86_HPET_WATCHDOG *owner = x86_watchdog_state();
    return !owner->Armed && !owner->Acquired && !owner->RouteOwned && !owner->Token.Id &&
        owner->Closed == 1 && !test_env.Window->Active && test_env.Window->Pool->FreePages == 42 &&
        test_hpet[0x10/4] == 0 && test_hpet[0xf0/4] == 1234 && test_hpet[0xf4/4] == 4 &&
        test_hpet[0x100/4] == 0x30 && test_hpet[0x108/4] == 0x89abcdef &&
        test_hpet[0x10c/4] == 0x12345678 && test_io[0x30] == 0x10040 && test_io[0x31] == 0;
}

// ====== run_watchdog_tests ======

int run_watchdog_tests(void)
{
    BOOT_U32 mode, result;
    watchdog_test_reset(0);
    if (x86_watchdog_open(NULL) || test_borrows || x86_watchdog_wait(test_source,1,NULL)) { return 1; }
    if (!x86_watchdog_open(&test_env) || x86_watchdog_open(&test_env)) { return 2; }
    for (mode = 1; mode <= 100; ++mode) {
        if (x86_watchdog_wait(test_source,mode,NULL) != 3 ||
            x86_watchdog_state()->Ticks != x86_watchdog_ticks(x86_watchdog_state()->Period,100) ||
            x86_watchdog_result() != X86_WATCHDOG_EVENT || test_source[0] != mode || test_in_irq ||
            test_env.Window->Active || test_env.Window->Pool->FreePages != 42) { return 3; }
    }
    if (!x86_watchdog_close() || !x86_watchdog_close() || !restored() || test_interrupts != 100) { return 4; }
    for (mode = 2; mode <= 3; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env)) { return 5; }
        result = x86_watchdog_wait(test_source,1,&test_stop);
        if ((mode == 2 && result) || (mode == 3 && !result) ||
            x86_watchdog_result() != (mode == 2 ? X86_WATCHDOG_TIMEOUT : X86_WATCHDOG_STOP) ||
            !x86_watchdog_close() || !restored()) { return 6; }
    }
    for (mode = 4; mode <= 13; ++mode) {
        watchdog_test_reset(mode);
        if (mode == 8) {
            if (!x86_watchdog_open(&test_env) || x86_watchdog_state()->Pin != 17 ||
                !x86_watchdog_close() || test_io[0x32] != 0x10040 || test_io[0x33]) { return 7; }
        } else if (x86_watchdog_open(&test_env) || test_env.Window->Active ||
            x86_watchdog_state()->Acquired || x86_watchdog_state()->RouteOwned) { return (int)(100+mode); }
    }
    watchdog_test_reset(14);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(test_source,1,NULL) ||
        x86_watchdog_result() != X86_WATCHDOG_FAULT || !x86_watchdog_close() || !restored()) { return 8; }
    watchdog_test_reset(15);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_close() || !x86_watchdog_state()->Acquired ||
        x86_watchdog_state()->Closed) { return 9; }
    test_mode = 0;
    if (!x86_watchdog_close() || !restored()) { return 10; }
    watchdog_test_reset(16);
    if (x86_watchdog_open(&test_env) || !restored()) { return 11; }
    watchdog_test_reset(17);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_close() || !x86_watchdog_state()->Token.Id ||
        !x86_watchdog_state()->Acquired || !x86_watchdog_state()->RouteOwned) { return 12; }
    test_mode = 0;
    if (!x86_watchdog_close() || !restored()) { return 13; }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(NULL,1,NULL) ||
        x86_watchdog_wait((volatile BOOT_U32 *)x86_watchdog_state(),1,NULL) ||
        x86_watchdog_wait(test_source,0,NULL) ||
        x86_watchdog_wait(test_source,1,test_source)) { return 14; }
    test_source[1] = 1;
    if (x86_watchdog_wait(test_source,1,NULL) || !x86_watchdog_close() || !restored()) { return 15; }
    for (mode = 18; mode <= 20; ++mode) {
        watchdog_test_reset(mode);
        if (x86_watchdog_open(&test_env) || !restored()) { return (int)(200+mode); }
    }
    for (mode = 21; mode <= 22; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env) || x86_watchdog_wait(test_source,1,NULL) ||
            !x86_watchdog_state()->Error || x86_watchdog_close() ||
            !x86_watchdog_state()->Token.Id || !x86_watchdog_state()->Acquired ||
            x86_watchdog_state()->Closed || test_env.Window->Active || !test_in_irq) { return (int)(200+mode); }
    }
    watchdog_test_reset(23);
    if (!x86_watchdog_open(&test_env) || !x86_watchdog_wait(test_source,1,&test_stop) ||
        x86_watchdog_result() != X86_WATCHDOG_STOP || test_source[0] ||
        !x86_watchdog_close() || !restored()) { return 16; }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) { return 17; }
    test_hpet[0xf0/4] = 0xffff0000;
    if (!x86_watchdog_wait(test_source,1,NULL) || x86_watchdog_state()->Target >= 0xffff0000 ||
        !x86_watchdog_close() || !restored()) { return 18; }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) { return 19; }
    x86_watchdog_state()->Count = ~0u;
    if (x86_watchdog_wait(test_source,1,NULL) || x86_watchdog_state()->Armed ||
        !x86_watchdog_close() || !restored()) { return 20; }
    watchdog_test_reset(2);
    if (!x86_watchdog_open(&test_env)) return 21;
    for (mode = 0; mode < 100; ++mode) {
        if (!x86_watchdog_idle(test_source) || x86_watchdog_result() != X86_WATCHDOG_IDLE ||
            x86_watchdog_state()->Ticks != x86_watchdog_ticks(x86_watchdog_state()->Period,5) ||
            x86_watchdog_failure() || test_source[0] || !x86_watchdog_valid()) return 22;
    }
    test_mode = 0;
    if (!x86_watchdog_idle(test_source) || x86_watchdog_result() != X86_WATCHDOG_EVENT ||
        !x86_watchdog_close() || !restored()) return 23;
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) return 24;
    test_source[1] = 1;
    if (x86_watchdog_idle(test_source) || !x86_watchdog_close() || !restored()) return 25;
    for (mode = 29; mode <= 30; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env) || !x86_watchdog_wait(test_source,1,NULL) ||
            x86_watchdog_failure() || test_source[0] != 1 ||
            !x86_watchdog_close() || !restored()) return (int)mode;
    }
    watchdog_test_reset(34);
    if (!x86_watchdog_open(&test_env) || x86_watchdog_idle(test_source) ||
        x86_watchdog_failure() != X86_WATCHDOG_WAKE || x86_watchdog_wake_detail() != 6 ||
        test_in_irq || x86_watchdog_state()->Armed || x86_watchdog_state()->Count != 1 ||
        test_source[0] != 1 || !x86_watchdog_state()->Acquired) return 34;
    test_mode = 0;
    if (!x86_watchdog_close() || !restored()) return 35;
    for (mode = 60; mode <= 70; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env)) return (int)(400 + mode);
        result = x86_watchdog_idle(test_source);
        if (mode <= 61) {
            if (!result || x86_watchdog_wake_detail() || x86_watchdog_failure() ||
                x86_watchdog_result() != X86_WATCHDOG_EVENT) return (int)(500 + mode);
        } else if (result || x86_watchdog_wake_detail() != 6 ||
            x86_watchdog_failure() != X86_WATCHDOG_WAKE) return (int)(600 + mode);
        if (test_in_irq || x86_watchdog_state()->Armed || x86_watchdog_state()->Count != 1)
            return (int)(700 + mode);
        if (mode <= 61 && (x86_watchdog_close() || !x86_watchdog_state()->Token.Id ||
            !x86_watchdog_state()->Acquired || x86_watchdog_state()->Closed)) return (int)(900 + mode);
        test_mode = 0;
        if (!x86_watchdog_close() || !restored()) return (int)(800 + mode);
    }
    watchdog_test_reset(80);
    if (!x86_watchdog_open(&test_env) || !x86_watchdog_idle(test_source) ||
        x86_watchdog_wake_detail() || x86_watchdog_failure() ||
        x86_watchdog_result() != X86_WATCHDOG_EVENT || x86_watchdog_close()) return 51;
    test_mode = 0;
    if (!x86_watchdog_close() || !restored()) return 52;
    for (mode = 71; mode <= 74; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env)) return (int)(1000 + mode);
        result = x86_watchdog_idle(test_source);
        if (mode <= 72) {
            if (result != (mode == 71 ? 17U : X86_WATCHDOG_WAKE_LIMIT) || x86_watchdog_failure() ||
                x86_watchdog_wake_detail()) return (int)(1100 + mode);
        } else if (result || x86_watchdog_failure() != X86_WATCHDOG_WAKE ||
            x86_watchdog_wake_detail() != 1) return (int)(1200 + mode);
        test_mode = 0;
        if (!x86_watchdog_close() || !restored()) return (int)(1300 + mode);
    }
    watchdog_test_reset(2);
    if (!x86_watchdog_open(&test_env) || !x86_watchdog_idle(test_source) ||
        x86_watchdog_wake_detail() || !x86_watchdog_close() || !restored()) return 36;
    watchdog_test_reset(35);
    if (!x86_watchdog_open(&test_env) || !x86_watchdog_idle(test_source)) return 37;
    test_source[0] = 0;
    test_mode = 0;
    if (!x86_watchdog_idle(test_source) || x86_watchdog_failure() ||
        x86_watchdog_state()->Armed || test_in_irq || test_interrupts != 2 ||
        x86_watchdog_state()->Count != 2 || test_hpet[0x10/4] != 0) return 38;
    if (!x86_watchdog_close() || !restored()) return 39;
    watchdog_test_reset(36);
    if (!x86_watchdog_open(&test_env)) return 40;
    test_hpet[0xf0/4] = 0xffff0000u;
    if (!x86_watchdog_idle(test_source) || x86_watchdog_failure() ||
        x86_watchdog_state()->Error || x86_watchdog_state()->Armed || test_in_irq ||
        x86_watchdog_state()->Count != 1 || test_interrupts != 2 ||
        !x86_watchdog_close() || !restored()) return 41;
    for (mode = 37; mode <= 40; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env)) return (int)(100 + mode);
        if (mode == 38) test_hpet[0xf0/4] = 0xffff0000u;
        if (x86_watchdog_idle(test_source) || x86_watchdog_failure() != X86_WATCHDOG_IRQ ||
            !x86_watchdog_state()->Error || !x86_watchdog_state()->Armed || !test_in_irq ||
            x86_watchdog_state()->Count || x86_watchdog_close() ||
            !x86_watchdog_state()->Acquired || !x86_watchdog_state()->Token.Id) return (int)mode;
    }
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env) || test_hpet[0x10/4]) return 42;
    test_hpet[0x20/4] = 3;
    if (x86_watchdog_idle(test_source) || x86_watchdog_arm_detail() != 13 ||
        test_hpet[0x20/4] != 3 || x86_watchdog_state()->Armed) return 43;
    test_hpet[0x20/4] = 0;
    if (!x86_watchdog_close() || !restored()) return 44;
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) return 45;
    test_hpet[0x20/4] = 1; test_hpet[0x100/4] ^= 2;
    if (x86_watchdog_idle(test_source) || x86_watchdog_arm_detail() != 14 ||
        test_hpet[0x20/4] != 1 || x86_watchdog_state()->Armed) return 46;
    test_hpet[0x100/4] ^= 2; test_hpet[0x20/4] = 0;
    if (!x86_watchdog_close() || !restored()) return 47;
    watchdog_test_reset(0);
    if (!x86_watchdog_open(&test_env)) return 48;
    test_mode = 21; test_hpet[0x20/4] = 1;
    if (x86_watchdog_idle(test_source) || x86_watchdog_arm_detail() != 13 ||
        test_hpet[0x20/4] != 1 || x86_watchdog_state()->Armed ||
        x86_watchdog_close() || !x86_watchdog_state()->Acquired) return 49;
    test_mode = 0; test_hpet[0x20/4] = 0;
    if (!x86_watchdog_close() || !restored()) return 50;
    for (mode = 41; mode <= 43; ++mode) {
        watchdog_test_reset(mode);
        if (!x86_watchdog_open(&test_env)) return (int)(100 + mode);
        test_hpet[0xf0/4] = 0xffff0000u;
        if (!x86_watchdog_idle(test_source) || x86_watchdog_failure() ||
            x86_watchdog_state()->Count != 1 || test_interrupts != 1 || test_in_irq ||
            x86_watchdog_state()->Armed || test_hpet[0x10/4] ||
            !x86_watchdog_close() || !restored()) return (int)(200 + mode);
    }
    return 0;
}

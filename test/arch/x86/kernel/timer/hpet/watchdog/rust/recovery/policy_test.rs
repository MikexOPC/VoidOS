#[path = "../../../../../../../../../arch/x86/kernel/timer/hpet/watchdog/rust/recovery/policy.rs"]
mod policy;
use policy::x86_watchdog_recovery_policy as decide;

// ====== normal_and_terminal_states ======

#[test]
fn normal_and_terminal_states() {
    assert_eq!(decide(0, 0, 0, 0), 0);
    assert_eq!(decide(0, 1, 0, 0), 1);
    assert_eq!(decide(241, 0, 0, 0), 2);
    assert_eq!(decide(241, 16, 0, 0), 3);
    assert_eq!(decide(3, 16, 20, 3), 6);
    assert_eq!(decide(3, 0, 23, 0), 6);
}

// ====== retry_is_only_cleanup ======

#[test]
fn retry_is_only_cleanup() {
    for phase in 1..=23 {
        assert_eq!(decide(113, 7, phase, 0), if (18..=22).contains(&phase) { 4 } else { 5 });
    }
    assert_eq!(decide(113, 7, 20, 2), 4);
    assert_eq!(decide(113, 7, 20, 3), 7);
    assert_eq!(decide(113, 13, 20, 0), 5);
    assert_eq!(decide(113, 14, 20, 0), 5);
}

// ====== malformed_snapshots_and_bounds ======

#[test]
fn malformed_snapshots_and_bounds() {
    for flags in 0..=255 {
        let status = decide(flags, 0, 20, 0);
        assert!(status <= 7);
        if flags & 2 != 0 && flags != 3 { assert_eq!(status, 5); }
        if flags & 1 == 0 { assert_eq!(status, 5); }
        if flags & 1 != 0 && flags & 2 == 0 && flags & 12 != 0 { assert_eq!(status, 5); }
    }
    assert_eq!(decide(256, 0, 0, 0), 5);
    assert_eq!(decide(241, 24, 0, 0), 5);
    assert_eq!(decide(241, 0, 24, 0), 5);
    assert_eq!(decide(113, 7, 20, 4), 5);
    assert_eq!(decide(241, u32::MAX, 0, 0), 5);
    assert_eq!(decide(129, 0, 0, 0), 5);
}

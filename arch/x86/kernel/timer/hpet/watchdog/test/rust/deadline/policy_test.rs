#[path = "../../../rust/deadline/policy.rs"]
mod policy;

// ====== deadline_bounds ======

#[test]
fn deadline_bounds() {
    assert_eq!(policy::x86_watchdog_ticks(10_000_000,100),10_000_000);
    assert_eq!(policy::x86_watchdog_ticks(100_000_000,1000),10_000_000);
    for (period,ms) in [(0,100),(100_000_001,100),(1,1000),(1,1),(10,0),(10,1001)] {
        assert_eq!(policy::x86_watchdog_ticks(period,ms),0);
    }
    for period in [500,1000,1_000_000,100_000_000] {
        let ticks = policy::x86_watchdog_ticks(period,1);
        assert!(u64::from(ticks)*u64::from(period) >= 1_000_000_000_000);
        assert!(u64::from(ticks-1)*u64::from(period) < 1_000_000_000_000);
    }
}

// ====== wrap_and_programming_margin ======

#[test]
fn wrap_and_programming_margin() {
    for start in [0,100,u32::MAX-10,u32::MAX] {
        assert_eq!(policy::x86_watchdog_ahead(start,start.wrapping_add(499),1000),1);
        assert_eq!(policy::x86_watchdog_ahead(start,start.wrapping_add(500),1000),0);
        assert_eq!(policy::x86_watchdog_ahead(start,start.wrapping_sub(1),1000),0);
    }
    for ticks in [0,1,0x80000000,u32::MAX] { assert_eq!(policy::x86_watchdog_ahead(0,0,ticks),0); }
}

// ====== event_timeout_stop_fault ======

#[test]
fn event_timeout_stop_fault() {
    for goal in [1,100,u32::MAX] {
        assert_eq!(policy::x86_watchdog_classify(goal,0,goal,0),1);
        assert_eq!(policy::x86_watchdog_classify(goal-1,0,goal,0),2);
        assert_eq!(policy::x86_watchdog_classify(goal,0,goal,1),4);
        assert_eq!(policy::x86_watchdog_classify(goal,1,goal,1),3);
        assert_eq!(policy::x86_watchdog_classify(goal,0,goal,2),3);
    }
    assert_eq!(policy::x86_watchdog_classify(2,0,1,0),3);
    assert_eq!(policy::x86_watchdog_classify(0,0,0,0),3);
}

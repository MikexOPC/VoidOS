#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/panic/rust/coordinator/state.rs"]
mod state;
use state::Coordinator;
use std::sync::{Arc, Barrier};

// ====== boundaries_and_lifetime ======

#[test]
fn boundaries_and_lifetime() {
    for count in [1, 2, 4, 64] {
        let s = Coordinator::new();
        assert_eq!(s.admit(0), 0);
        assert_eq!(s.record_state(64), 3);
        assert_eq!(s.configure(0), 0);
        assert_eq!(s.configure(65), 0);
        assert_eq!(s.configure(count), 1);
        assert_eq!(s.configure(count), 0);
        let mask = if count == 64 { u64::MAX } else { (1 << count) - 1 };
        assert_eq!(s.value(1), mask);
        assert_eq!(s.value(2), 0);
        assert_eq!(s.elect(0), 0);
        assert_eq!(s.admit(0), 1);
        assert_eq!(s.admit(0), 0);
        assert_eq!(s.admit(count), 0);
        assert_eq!(s.elect(0), 1);
        assert_eq!(s.elect(0), 3);
        assert_eq!(s.value(4), 0);
        assert_eq!(s.finish(0, 32), 0);
        assert_eq!(s.finish(0, 0), 1);
        assert_eq!(s.finish(0, 0), 0);
        assert_eq!(s.value(7), 2);
    }
}

// ====== incomplete_and_late_ack ======

#[test]
fn incomplete_and_late_ack() {
    let s = Coordinator::new();
    assert_eq!(s.configure(4), 1);
    for index in 0..4 { assert_eq!(s.admit(index), 1); }
    assert_eq!(s.elect(0), 1);
    assert_eq!(s.acknowledge(1), 0);
    assert_eq!(s.record_publish(1), 0);
    assert_eq!(s.record_claim(1), 1);
    assert_eq!(s.record_claim(1), 0);
    assert_eq!(s.acknowledge(1), 0);
    assert_eq!(s.record_publish(1), 1);
    assert_eq!(s.record_publish(1), 0);
    assert_eq!(s.acknowledge(1), 1);
    assert_eq!(s.acknowledge(1), 0);
    assert_eq!(s.acknowledge(0), 0);
    assert_eq!(s.finish(1, 0), 0);
    assert_eq!(s.finish(0, 0), 1);
    assert_eq!(s.value(6), 12);
    assert_eq!(s.value(8), 1);
    for index in [2, 3] {
        assert_eq!(s.record_claim(index), 1);
        assert_eq!(s.record_publish(index), 1);
        assert_eq!(s.acknowledge(index), 1);
    }
    assert_eq!(s.value(5), 14);
    assert_eq!(s.value(6), 12);
    assert_eq!(s.value(8), 1);
}

// ====== concurrent_election ======

#[test]
fn concurrent_election() {
    for count in [1, 2, 4, 64] {
        for _ in 0..20 {
            let s = Arc::new(Coordinator::new());
            assert_eq!(s.configure(count), 1);
            let barrier = Arc::new(Barrier::new(count as usize));
            let mut jobs = Vec::new();
            for index in 0..count {
                let s = s.clone(); let barrier = barrier.clone();
                jobs.push(std::thread::spawn(move || {
                    assert_eq!(s.admit(index), 1);
                    assert_eq!(s.record_claim(index), 1);
                    assert_eq!(s.record_publish(index), 1);
                    barrier.wait();
                    let result = s.elect(index);
                    if result == 2 { assert_eq!(s.acknowledge(index), 1); }
                    result
                }));
            }
            let wins = jobs.into_iter().map(|j| j.join().unwrap()).filter(|r| *r == 1).count();
            assert_eq!(wins, 1);
            let owner = s.value(3) as u32 - 1;
            assert_eq!(s.finish(owner, 0), 1);
            assert_eq!(s.value(6), 0);
            assert_eq!(s.value(8), 0);
            assert_eq!(s.value(4) & (1 << owner), 0);
        }
    }
}

// ====== admission_race ======

#[test]
fn admission_race() {
    for _ in 0..1000 {
        let s = Arc::new(Coordinator::new());
        assert_eq!(s.configure(2), 1);
        assert_eq!(s.admit(0), 1);
        let b = Arc::new(Barrier::new(2));
        let other = s.clone(); let other_b = b.clone();
        let job = std::thread::spawn(move || { other_b.wait(); other.admit(1) });
        b.wait(); assert_eq!(s.elect(0), 1);
        let admitted = job.join().unwrap();
        assert!(admitted == 1 || admitted == 2);
        if admitted == 1 { assert_eq!(s.value(4), 2); }
        assert_eq!(s.admit(1), 2);
    }
}

// ====== sequentially_consistent_admission_orders ======

#[test]
fn sequentially_consistent_admission_orders() {
    let mut checked = 0;
    for join in 0..4 {
        for recheck in 0..4 {
            for election in 0..4 {
                for snapshot in 0..4 {
                    let order = [join, recheck, election, snapshot];
                    if (0..4).any(|a| (a + 1..4).any(|b| order[a] == order[b])) { continue; }
                    if join >= recheck || election >= snapshot { continue; }
                    let returned_normally = recheck < election;
                    let targeted = join < snapshot;
                    assert!(!returned_normally || targeted);
                    checked += 1;
                }
            }
        }
    }
    assert_eq!(checked, 6);
}

// ====== registered_origin_before_online ======

#[test]
fn registered_origin_before_online() {
    let s = Coordinator::new();
    assert_eq!(s.configure(2), 1);
    assert_eq!(s.admit(0), 1);
    assert_eq!(s.elect(1), 0);
    assert_eq!(s.record_claim(1), 1);
    assert_eq!(s.elect(1), 1);
    assert_eq!(s.value(2), 1);
    assert_eq!(s.value(4), 1);
    assert_eq!(s.record_state(1), 1);
    assert_eq!(s.record_claim(0), 1);
    assert_eq!(s.record_publish(0), 1);
    assert_eq!(s.acknowledge(0), 1);
    assert_eq!(s.finish(1, 0), 1);
    assert_eq!(s.value(6), 0);
}

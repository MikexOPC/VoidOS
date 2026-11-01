#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/startup/rust/protocol/state.rs"]
mod state;
use state::*;
use std::sync::atomic::{AtomicU32, Ordering::Relaxed};

// ====== configuration_and_bounds ======

#[test]
fn configuration_and_bounds() {
    let s = Startup::new();
    assert_eq!(s.reserve(1), 0);
    for count in [0, 65, u32::MAX] { assert_eq!(s.configure(count), 0); }
    assert_eq!(s.configure(64), 1);
    assert_eq!(s.configure(4), 0);
    for cpu in [0, 2, 63, 64, u32::MAX] { assert_eq!(s.reserve(cpu), 0); }
    assert_ne!(s.reserve(1), 0);
}

// ====== sequential_all_cpus ======

#[test]
fn sequential_all_cpus() {
    let s = Startup::new();
    assert_eq!(s.configure(64), 1);
    for cpu in 1..64 {
        let token = s.reserve(cpu);
        assert_ne!(token, 0);
        for phase in PREPARED..=PRIVATE { assert_eq!(s.advance(token, phase), 1); }
        assert_eq!(s.snapshot(), token | PARKED);
        assert_eq!(s.cancel(token), 0);
        assert_eq!(s.quarantine(token), 0);
    }
    assert_eq!(s.reserve(64), 0);
}

// ====== cancellation_preserves_generation ======

#[test]
fn cancellation_preserves_generation() {
    let s = Startup::new();
    s.configure(4);
    let old = s.reserve(1);
    assert_eq!(s.cancel(old), 1);
    assert_eq!(s.cancel(old), 0);
    let new = s.reserve(1);
    assert_ne!(old, new);
    assert_eq!(s.advance(old, PREPARED), 0);
    assert_eq!(s.quarantine(old), 0);
    assert_eq!(s.advance(new, PREPARED), 1);
    assert_eq!(s.cancel(new), 0);
}

// ====== reject_skips_duplicates_and_bad_tokens ======

#[test]
fn reject_skips_duplicates_and_bad_tokens() {
    let s = Startup::new();
    s.configure(4);
    let token = s.reserve(1);
    for bad in [0, token | 1, token + 8, token + 512, u64::MAX, 1u64 << 63] {
        assert_eq!(s.advance(bad, PREPARED), 0);
        assert_eq!(s.cancel(bad), 0);
        assert_eq!(s.quarantine(bad), 0);
    }
    for phase in [0, COMMITTED, ARMED, ENTERED, PRIVATE, PARKED, QUARANTINED, u64::MAX] {
        assert_eq!(s.advance(token, phase), 0);
    }
    assert_eq!(s.quarantine(token), 0);
    assert_eq!(s.reserve(2), 0);
    assert_eq!(s.advance(token, PREPARED), 1);
    assert_eq!(s.advance(token, PREPARED), 0);
}

// ====== quarantine_is_terminal_and_rejects_late_ap ======

#[test]
fn quarantine_is_terminal_and_rejects_late_ap() {
    for until in COMMITTED..=PRIVATE {
        let s = Startup::new();
        s.configure(4);
        let token = s.reserve(1);
        for phase in PREPARED..until { assert_eq!(s.advance(token, phase), 1); }
        assert_eq!(s.quarantine(token), 1);
        for phase in PREPARED..=QUARANTINED { assert_eq!(s.advance(token, phase), 0); }
        assert_eq!(s.cancel(token), 0);
        assert_eq!(s.quarantine(token), 0);
        assert_eq!(s.reserve(1), 0);
        assert_eq!(s.reserve(2), 0);
        assert_eq!(s.snapshot(), token | QUARANTINED);
    }
}

// ====== single_reservation_winner ======

#[test]
fn single_reservation_winner() {
    let s = Startup::new();
    s.configure(4);
    std::thread::scope(|scope| {
        let threads: Vec<_> = (0..16).map(|_| scope.spawn(|| s.reserve(1))).collect();
        assert_eq!(threads.into_iter().map(|t| t.join().unwrap()).filter(|t| *t != 0).count(), 1);
    });
}

// ====== parked_vs_timeout_has_one_terminal_winner ======

#[test]
fn parked_vs_timeout_has_one_terminal_winner() {
    for _ in 0..128 {
        let s = Startup::new();
        s.configure(4);
        let token = s.reserve(1);
        for phase in PREPARED..PRIVATE { assert_eq!(s.advance(token, phase), 1); }
        std::thread::scope(|scope| {
            let park = scope.spawn(|| s.advance(token, PRIVATE));
            let timeout = scope.spawn(|| s.quarantine(token));
            assert_eq!(park.join().unwrap() + timeout.join().unwrap(), 1);
        });
        match s.snapshot() & 7 {
            PARKED => assert_ne!(s.reserve(2), 0),
            QUARANTINED => assert_eq!(s.reserve(2), 0),
            other => panic!("Unexpected terminal state {other}"),
        }
    }
}

// ====== publication_before_ap_entry ======

#[test]
fn publication_before_ap_entry() {
    let s = Startup::new();
    s.configure(4);
    let token = s.reserve(1);
    let payload = AtomicU32::new(0);
    let start = std::sync::Barrier::new(2);
    std::thread::scope(|scope| {
        let publisher = scope.spawn(|| {
            start.wait();
            payload.store(42, Relaxed);
            assert_eq!(s.advance(token, PREPARED), 1);
            assert_eq!(s.advance(token, COMMITTED), 1);
        });
        let ap = scope.spawn(|| {
            start.wait();
            let deadline = std::time::Instant::now() + std::time::Duration::from_secs(10);
            while s.advance(token, ARMED) == 0 {
                assert!(std::time::Instant::now() < deadline, "Publisher made no progress");
                std::thread::yield_now();
            }
            assert_eq!(payload.load(Relaxed), 42);
        });
        publisher.join().unwrap();
        ap.join().unwrap();
    });
}

// ====== rollback_next_cpu_and_reject_old_timeout ======

#[test]
fn rollback_next_cpu_and_reject_old_timeout() {
    let s = Startup::new();
    s.configure(4);
    let first = s.reserve(1);
    for phase in PREPARED..=PRIVATE { assert_eq!(s.advance(first, phase), 1); }
    let second = s.reserve(2);
    assert_eq!(s.cancel(second), 1);
    assert_eq!(s.reserve(3), 0);
    let retry = s.reserve(2);
    assert_ne!(second, retry);
    assert_eq!(s.quarantine(first), 0);
    assert_eq!(s.quarantine(second), 0);
    assert_eq!(s.snapshot(), retry | PREPARED);
    let bsp = Startup::new();
    assert_eq!(bsp.configure(1), 1);
    assert_eq!(bsp.reserve(1), 0);
}

// ====== cancellation_vs_commit_has_one_winner ======

#[test]
fn cancellation_vs_commit_has_one_winner() {
    for _ in 0..128 {
        let s = Startup::new();
        s.configure(4);
        let token = s.reserve(1);
        std::thread::scope(|scope| {
            let cancel = scope.spawn(|| s.cancel(token));
            let commit = scope.spawn(|| s.advance(token, PREPARED));
            assert_eq!(cancel.join().unwrap() + commit.join().unwrap(), 1);
        });
        if s.snapshot() & 7 == COMMITTED {
            assert_eq!(s.reserve(1), 0);
            assert_eq!(s.cancel(token), 0);
        } else { assert_ne!(s.reserve(1), 0); }
    }
}

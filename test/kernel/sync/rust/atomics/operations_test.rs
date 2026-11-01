#[path = "../../../../../kernel/sync/rust/atomics/operations.rs"]
mod operations;
use operations::*;
use std::sync::{Arc, atomic::AtomicU64};

// ====== policy_limits ======

#[test]
fn policy_limits() {
    assert_eq!(ks_rank_enter(0, 1, 0), 1);
    assert_eq!(ks_rank_enter(1, 2, 1), 1);
    for (held, next, depth) in [(1,1,1),(2,1,1),(0,0,0),(1,2,0),(0,1,1),(1,2,8),(0,65536,0)] {
        assert_eq!(ks_rank_enter(held, next, depth), 0);
    }
    assert_eq!(ks_ticket_available(u64::MAX - 1), 1);
    assert_eq!(ks_ticket_available(u64::MAX), 0);
}

// ====== ffi_roundtrip ======

#[test]
fn ffi_roundtrip() {
    let value = AtomicU64::new(0);
    // SAFETY: AtomicU64 owns aligned live storage; all accesses below are atomic64.
    unsafe {
        ks_atomic_store_relaxed(value.as_ptr(), u64::MAX - 1);
        assert_eq!(ks_atomic_load_relaxed(value.as_ptr()), u64::MAX - 1);
        assert_eq!(ks_atomic_compare(value.as_ptr(), 0, 1), 0);
        assert_eq!(ks_atomic_compare(value.as_ptr(), u64::MAX - 1, 7), 1);
        ks_atomic_store_release(value.as_ptr(), 19);
        assert_eq!(ks_atomic_load_acquire(value.as_ptr()), 19);
    }
}

// ====== concurrent_cas ======

#[test]
fn concurrent_cas() {
    let value = Arc::new(AtomicU64::new(0));
    let handles: Vec<_> = (0..4).map(|_| {
        let shared = value.clone();
        std::thread::spawn(move || {
            for _ in 0..10_000 {
                loop {
                    // SAFETY: Arc keeps aligned AtomicU64 storage live; no mixed-size/plain access.
                    let success = unsafe {
                        let previous = ks_atomic_load_relaxed(shared.as_ptr());
                        ks_atomic_compare(shared.as_ptr(), previous, previous + 1)
                    };
                    if success != 0 { break; }
                    std::hint::spin_loop();
                }
            }
        })
    }).collect();
    for handle in handles { handle.join().unwrap(); }
    assert_eq!(value.load(std::sync::atomic::Ordering::Acquire), 40_000);
}

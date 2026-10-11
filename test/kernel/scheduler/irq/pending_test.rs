use super::*;
use std::sync::Arc;

// ====== pending_bounds_coalescing_and_restore ======

#[test]
fn pending_bounds_coalescing_and_restore() {
    let pending = Pending::new();
    assert!(pending.empty());
    assert!(!pending.contains(8)); assert!(!pending.contains(u32::MAX));
    for event in [8, u32::MAX] { assert!(!pending.post(event)); }
    for event in 0..8 {
        for _ in 0..1000 { assert!(pending.post(event)); }
        assert!(pending.contains(event));
    }
    assert!(!pending.empty()); assert_eq!(pending.take(), 255); assert!(pending.empty());
    assert!(pending.post(7)); assert!(!pending.restore(256)); assert_eq!(pending.take(), 128);
    assert!(pending.restore(5)); assert!(pending.post(1)); assert_eq!(pending.take(), 7);
    assert_eq!(pending.take(), 0);
}

// ====== concurrent_publication_and_drain ======

#[test]
fn concurrent_publication_and_drain() {
    let pending = Arc::new(Pending::new());
    let producers: Vec<_> = (0..8).map(|event| {
        let pending = Arc::clone(&pending);
        std::thread::spawn(move || { for _ in 0..10000 { assert!(pending.post(event)); } })
    }).collect();
    let mut observed = 0;
    // Coalescing allows many posts in one bit; no per-post cardinality claim.
    for _ in 0..10000 { observed |= pending.take(); }
    for producer in producers { producer.join().unwrap(); }
    observed |= pending.take(); assert_eq!(observed, 255); assert!(pending.empty());
}

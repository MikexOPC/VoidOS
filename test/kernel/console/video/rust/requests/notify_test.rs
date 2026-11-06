use super::Notify;

// ====== coalescing_and_rearm ======

#[test]
fn coalescing_and_rearm() {
    let owner = Notify::default();
    assert!(!owner.publish(1, 9));
    assert!(!owner.publish(0, 0));
    assert!(owner.publish(0, 9));
    assert!(!owner.publish(0, 10));
    assert_eq!(owner.begin(0, 9, 0), 0);
    assert_eq!(owner.begin(1, 8, 0), 0);
    assert_eq!(owner.begin(1, 9, 2), 0);
    let first = owner.begin(1, 9, 0);
    let second = owner.begin(1, 9, 1);
    assert_eq!(first, 3);
    assert_eq!(second, 4);
    assert_eq!(owner.begin(1, 9, 0), 0);
    assert!(!owner.finish(1, 9, 0, second));
    assert!(owner.finish(1, 9, 0, first));
    assert!(!owner.finish(1, 9, 0, first));
    assert_eq!(owner.take(0, 9), 2);
    let third = owner.begin(1, 9, 0);
    assert_eq!(third, 7);
    assert!(owner.acknowledge(0, 9, 2));
    assert_eq!(owner.query(9, 3), 1);
    assert!(!owner.acknowledge(0, 9, 4));
    assert!(owner.finish(1, 9, 0, third));
    assert!(owner.finish(1, 9, 1, second));
    assert_eq!(owner.take(0, 9), 3);
    assert!(owner.acknowledge(0, 9, 3));
    assert!(owner.acknowledge(0, 9, 1));
    assert_eq!(owner.query(9, 2), 3);
    assert!(owner.seal(0, 9));
    assert!(!owner.close(0, 9, false, true));
    assert!(!owner.close(0, 9, true, false));
    assert!(owner.close(0, 9, true, true));
    assert_eq!(owner.query(9, 0), 3);
    assert_eq!(owner.take(0, 9), u64::MAX);
    assert_eq!(owner.begin(1, 9, 0), 0);
    assert!(!owner.seal(0, 9));
    assert!(!owner.publish(0, 11));
}

// ====== close_requires_sender_and_ack ======

#[test]
fn close_requires_sender_and_ack() {
    let owner = Notify::new();
    assert!(owner.publish(0, 1));
    let notice = owner.begin(1, 1, 0);
    assert!(owner.seal(0, 1));
    assert_eq!(owner.begin(1, 1, 1), 0);
    assert!(!owner.close(0, 1, true, true));
    assert_eq!(owner.take(0, 1), 1);
    assert!(owner.acknowledge(0, 1, 1));
    assert!(!owner.close(0, 1, true, true));
    assert!(owner.finish(1, 1, 0, notice));
    assert!(owner.close(0, 1, true, true));
}

// ====== overflow_is_terminal ======

#[test]
fn overflow_is_terminal() {
    use core::sync::atomic::Ordering;
    let owner = Notify::new();
    assert!(owner.publish(0, 1));
    owner.published.store(u64::MAX / 2, Ordering::Relaxed);
    assert_eq!(owner.begin(1, 1, 0), 0);
    assert_eq!(owner.query(1, 0) & 4, 4);
    assert!(!owner.seal(0, 1));
    assert!(!owner.close(0, 1, true, true));
}

// ====== seal_vs_sender ======

#[test]
fn seal_vs_sender() {
    for _ in 0..500 {
        let owner = Notify::new();
        let barrier = std::sync::Barrier::new(2);
        assert!(owner.publish(0, 1));
        std::thread::scope(|scope| {
            let sender = scope.spawn(|| {
                barrier.wait();
                let notice = owner.begin(1, 1, 0);
                if notice != 0 { assert!(owner.finish(1, 1, 0, notice)); }
                notice
            });
            barrier.wait();
            assert!(owner.seal(0, 1));
            let closed = owner.close(0, 1, true, true);
            let notice = sender.join().unwrap();
            if closed { assert_eq!(notice, 0); }
            if notice != 0 {
                assert!(!closed);
                assert!(!owner.close(0, 1, true, true));
                let sequence = owner.take(0, 1);
                assert!(owner.acknowledge(0, 1, sequence));
                assert!(owner.close(0, 1, true, true));
            }
        });
    }
}

// ====== rejected_calls_preserve_state ======

#[test]
fn rejected_calls_preserve_state() {
    let owner = Notify::new();
    assert_eq!(owner.take(0, 1), u64::MAX);
    assert_eq!(owner.query(1, 0), u64::MAX);
    assert!(owner.publish(0, 1));
    let snapshot: Vec<_> = (0..6).map(|field| owner.query(1, field)).collect();
    for cpu in [1, 2, u32::MAX] {
        assert_eq!(owner.take(cpu, 1), u64::MAX);
        assert!(!owner.acknowledge(cpu, 1, 0));
        assert!(!owner.seal(cpu, 1));
        assert!(!owner.close(cpu, 1, true, true));
    }
    for token in [0, 2, u64::MAX] {
        assert_eq!(owner.begin(1, token, 0), 0);
        assert!(!owner.finish(1, token, 0, 3));
        assert_eq!(owner.take(0, token), u64::MAX);
        assert!(!owner.acknowledge(0, token, 0));
        assert!(!owner.seal(0, token));
        assert!(!owner.close(0, token, true, true));
    }
    assert_eq!(snapshot, (0..6).map(|field| owner.query(1, field)).collect::<Vec<_>>());
    assert_eq!(owner.query(1, 6), u64::MAX);
}

// ====== last_sequence_has_no_wrap ======

#[test]
fn last_sequence_has_no_wrap() {
    use core::sync::atomic::Ordering;
    let owner = Notify::new();
    assert!(owner.publish(0, 1));
    owner.published.store(u64::MAX / 2 - 1, Ordering::Relaxed);
    let notice = owner.begin(1, 1, 0);
    assert_eq!(notice, u64::MAX);
    assert!(owner.finish(1, 1, 0, notice));
    assert_eq!(owner.take(0, 1), u64::MAX / 2);
    assert!(owner.acknowledge(0, 1, u64::MAX / 2));
    assert!(owner.seal(0, 1));
    assert!(owner.close(0, 1, true, true));
}

// ====== concurrent_publication_and_take ======

#[test]
fn concurrent_publication_and_take() {
    use core::sync::atomic::{AtomicU32, Ordering};
    let owner = Notify::new();
    let done = AtomicU32::new(0);
    let sends = AtomicU32::new(0);
    let barrier = std::sync::Barrier::new(3);
    assert!(owner.publish(0, 1));
    std::thread::scope(|scope| {
        for producer in 0..2 {
            let (owner, done, sends, barrier) = (&owner, &done, &sends, &barrier);
            scope.spawn(move || {
                barrier.wait();
                for _ in 0..2000 {
                    let mut notice = 0;
                    for _ in 0..1024 {
                        notice = owner.begin(1, 1, producer);
                        if notice != 0 { break; }
                        std::thread::yield_now();
                    }
                    assert_ne!(notice, 0, "bounded sender exhausted");
                    if notice & 1 != 0 { sends.fetch_add(1, Ordering::Relaxed); }
                    assert!(owner.finish(1, 1, producer, notice));
                    std::thread::yield_now();
                }
                done.fetch_add(1, Ordering::Release);
            });
        }
        barrier.wait();
        let mut finished = false;
        for _ in 0..1_000_000 {
            let sequence = owner.take(0, 1);
            assert!(owner.acknowledge(0, 1, sequence));
            if done.load(Ordering::Acquire) == 2 { finished = true; break; }
            std::thread::yield_now();
        }
        assert!(finished, "bounded receiver exhausted");
    });
    assert_eq!(owner.query(1, 1), 4000);
    assert!((1..=4000).contains(&sends.load(Ordering::Relaxed)));
    assert!(owner.seal(0, 1));
    let sequence = owner.take(0, 1);
    assert!(owner.acknowledge(0, 1, sequence));
    assert!(owner.close(0, 1, true, true));
}

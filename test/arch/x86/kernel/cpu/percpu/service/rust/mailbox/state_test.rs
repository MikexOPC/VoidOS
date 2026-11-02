use super::*;
use std::{sync::Arc, thread, time::{Duration, Instant}};

// ====== prepared ======

fn prepared() -> Service {
    let service = Service::new();
    assert_eq!(service.configure(4), 1);
    for index in 1..4 { assert_eq!(service.ready(index, index), 1); }
    service
}

// ====== configuration_and_bounds ======

#[test]
fn configuration_and_bounds() {
    let service = Service::new();
    assert_eq!(service.ready(1, 1), 0);
    assert_eq!(service.configure(0), 0);
    assert_eq!(service.configure(65), 0);
    assert_eq!(service.configure(64), 1);
    assert_eq!(service.configure(4), 0);
    for index in [0, 64, u32::MAX] {
        assert_eq!(service.ready(index, index), 0);
        assert_eq!(service.publish(index, 0), INVALID);
        assert_eq!(service.claim(index, index), INVALID);
        assert_eq!(service.complete(index, index, 1), 0);
        assert_eq!(service.fail(index, 0), 0);
        assert_eq!(service.value(index, 0), INVALID);
    }
    assert_eq!(service.ready(63, 63), 1);
    assert_eq!(service.value(63, 9), INVALID);
    let bsp = Service::new();
    assert_eq!(bsp.configure(1), 1);
    assert_eq!(bsp.ready(1, 1), 0);
}

// ====== ownership_and_lifecycle ======

#[test]
fn ownership_and_lifecycle() {
    let service = Service::new();
    assert_eq!(service.configure(4), 1);
    assert_eq!(service.publish(1, 0), INVALID);
    assert_eq!(service.ready(1, 0), 0);
    assert_eq!(service.ready(1, 1), 1);
    assert_eq!(service.ready(1, 1), 0);
    assert_eq!(service.publish(1, 1), INVALID);
    assert_eq!(service.claim(1, 2), INVALID);
    assert_eq!(service.fail(1, 2), 0);
    assert_eq!(service.claim(1, 1), 0);
    assert_eq!(service.publish(1, 0), 1);
    assert_eq!(service.publish(1, 0), 0);
    assert_eq!(service.complete(1, 1, 1), 0);
    assert_eq!(service.claim(1, 1), 1);
    assert_eq!(service.claim(1, 1), INVALID);
    assert_eq!(service.complete(1, 2, 1), 0);
    for token in [0, 2, INVALID] { assert_eq!(service.complete(1, 1, token), 0); }
    assert_eq!(service.complete(1, 1, 1), 1);
    assert_eq!(service.complete(1, 1, 1), 0);
    assert_eq!(service.claim(1, 1), 0);
    assert_eq!(service.publish(1, 0), 2);
    assert_eq!(service.claim(1, 1), 2);
    assert_eq!(service.complete(1, 1, 1), 0);
    assert_eq!(service.complete(1, 1, 2), 1);
    for field in 1..=3 { assert_eq!(service.value(1, field), 2); }
    assert_eq!(service.value(2, 1), 0);
}

// ====== failure_is_terminal ======

#[test]
fn failure_is_terminal() {
    for stage in 0..4 {
        let service = Service::new();
        assert_eq!(service.configure(4), 1);
        if stage > 0 { assert_eq!(service.ready(1, 1), 1); }
        if stage > 1 { assert_eq!(service.publish(1, 0), 1); }
        if stage > 2 { assert_eq!(service.claim(1, 1), 1); }
        assert_eq!(service.fail(1, if stage % 2 == 0 { 0 } else { 1 }), 1);
        assert_eq!(service.value(1, 0), 2);
        assert_eq!(service.fail(1, 0), 0);
        assert_eq!(service.ready(1, 1), 0);
        assert_eq!(service.publish(1, 0), INVALID);
        assert_eq!(service.claim(1, 1), INVALID);
        assert_eq!(service.complete(1, 1, 1), 0);
        assert_eq!(service.value(1, 3), 0);
    }
}

// ====== generation_exhaustion ======

#[test]
fn generation_exhaustion() {
    let service = prepared();
    let slot = &service.slots[1];
    let last = INVALID - 2;
    slot.request.store(last, Release);
    slot.claimed.store(last, Release);
    slot.ack.store(last, Release);
    assert_eq!(service.publish(1, 0), INVALID - 1);
    assert_eq!(service.claim(1, 1), INVALID - 1);
    assert_eq!(service.complete(1, 1, INVALID - 1), 1);
    assert_eq!(service.publish(1, 0), INVALID);
    assert_eq!(service.value(1, 1), INVALID - 1);
}

// ====== concurrent_three_consumers ======

#[test]
fn concurrent_three_consumers() {
    let service = Arc::new(prepared());
    let limit = Instant::now() + Duration::from_secs(20);
    let jobs: Vec<_> = (1..4).map(|index| {
        let service = Arc::clone(&service);
        thread::spawn(move || {
            for expected in 1..=10_000 {
                loop {
                    assert!(Instant::now() < limit);
                    let token = service.claim(index, index);
                    if token == 0 { thread::yield_now(); continue; }
                    assert_eq!(token, expected);
                    assert_eq!(service.complete(index, index, token), 1);
                    break;
                }
            }
        })
    }).collect();
    for expected in 1..=10_000 {
        for index in 1..4 {
            loop {
                assert!(Instant::now() < limit);
                let token = service.publish(index, 0);
                if token == 0 { thread::yield_now(); continue; }
                assert_eq!(token, expected);
                break;
            }
        }
    }
    for job in jobs { job.join().unwrap(); }
    for index in 1..4 { assert_eq!(service.value(index, 3), 10_000); }
}

// ====== timeout_racing_completion_cannot_reopen ======

#[test]
fn timeout_racing_completion_cannot_reopen() {
    for _ in 0..200 {
        let service = Arc::new(prepared());
        assert_eq!(service.publish(1, 0), 1);
        assert_eq!(service.claim(1, 1), 1);
        let other = Arc::clone(&service);
        let job = thread::spawn(move || other.complete(1, 1, 1));
        assert_eq!(service.fail(1, 0), 1);
        assert!(job.join().unwrap() <= 1);
        assert_eq!(service.value(1, 0), 2);
        assert_eq!(service.publish(1, 0), INVALID);
        assert_eq!(service.ready(1, 1), 0);
    }
}

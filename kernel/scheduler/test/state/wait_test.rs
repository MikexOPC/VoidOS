use super::*;
use crate::{CAPACITY, NONE};

// ====== active_pair ======

fn pair() -> (Owner, Handle, Handle) {
    let mut owner = Owner::new();
    let a = owner.reserve().unwrap(); let b = owner.reserve().unwrap();
    owner.publish(a).unwrap(); owner.publish(b).unwrap();
    assert_eq!(owner.select(), Ok(a));
    (owner, a, b)
}

// ====== early_signal_and_registered_wait ======

#[test]
fn early_signal_and_registered_wait() {
    let (mut owner, a, b) = pair();
    let token = owner.observe(a, 0).unwrap();
    owner.yield_task(a).unwrap(); assert_eq!(owner.select(), Ok(b));
    assert_eq!(owner.signal(b, 0).unwrap().woken, 0);
    owner.yield_task(b).unwrap(); assert_eq!(owner.select(), Ok(a));
    assert_eq!(owner.wait(a, 0, token.epoch).unwrap().outcome, 2);
    assert_eq!(owner.select(), Ok(b)); owner.yield_task(b).unwrap();
    assert_eq!(owner.select(), Ok(a));
    assert_eq!(owner.wait(a, 1, 0).unwrap().outcome, 1);
    assert_eq!(owner.state(a), Ok(State::Waiting));
    assert_eq!(owner.select(), Ok(b));
    assert_eq!(owner.signal(b, 0).unwrap().woken, 0);
    assert_eq!(owner.state(a), Ok(State::Waiting));
    let result = owner.signal(b, 1).unwrap();
    assert_eq!((result.epoch, result.woken), (1, 1));
    assert_eq!(owner.signal(b, 1).unwrap().woken, 0);
    owner.yield_task(b).unwrap(); assert_eq!(owner.select(), Ok(a));
    assert!(owner.validate());
}

// ====== wake_all_and_order ======

#[test]
fn wake_all_and_order() {
    let mut owner = Owner::new();
    let mut handles = Vec::new();
    for _ in 0..CAPACITY { handles.push(owner.reserve().unwrap()); }
    for &handle in &handles { owner.publish(handle).unwrap(); }
    for &handle in handles.iter().take(CAPACITY - 1) {
        assert_eq!(owner.select(), Ok(handle)); owner.wait(handle, 7, 0).unwrap();
    }
    let producer = owner.select().unwrap();
    assert_eq!(owner.signal(producer, 7).unwrap().woken, 7);
    owner.yield_task(producer).unwrap();
    for &handle in &handles {
        assert_eq!(owner.select(), Ok(handle)); owner.exit(handle).unwrap(); owner.collect(handle).unwrap();
    }
    assert_eq!(owner.select(), Err(Error::Empty)); assert!(owner.validate());
}

// ====== blocked_is_not_empty ======

#[test]
fn blocked_is_not_empty() {
    let (mut owner, a, b) = pair();
    owner.wait(a, 0, 0).unwrap(); assert_eq!(owner.select(), Ok(b)); owner.wait(b, 1, 0).unwrap();
    let before = owner.copy_state();
    assert_eq!(owner.select(), Err(Error::Blocked));
    assert_eq!(owner.collect(a), Err(Error::InvalidState));
    assert_eq!(owner.abort(a), Err(Error::InvalidState));
    assert_eq!(owner.publish(a), Err(Error::InvalidState));
    assert_eq!(owner, before);
    let mut full = Owner::new();
    let handles: Vec<_> = (0..crate::CAPACITY).map(|_| full.reserve().unwrap()).collect();
    for &handle in &handles { full.publish(handle).unwrap(); }
    for &handle in &handles {
        assert_eq!(full.select(), Ok(handle)); full.wait(handle, 7, 0).unwrap();
    }
    assert_eq!(full.select(), Err(Error::Blocked)); assert!(full.validate());
}

// ====== rejected_event_commands_are_immutable ======

#[test]
fn rejected_event_commands_are_immutable() {
    let (mut owner, a, b) = pair();
    let bad = [Handle {index: NONE, generation: 1}, Handle {index: a.index, generation: 0},
        Handle {index: a.index, generation: a.generation + 1}, b];
    for handle in bad {
        let before = owner.copy_state();
        assert!(owner.observe(handle, 0).is_err()); assert!(owner.wait(handle, 0, 0).is_err());
        assert!(owner.signal(handle, 0).is_err()); assert_eq!(owner, before);
    }
    for event in [8, u32::MAX] {
        let before = owner.copy_state();
        assert_eq!(owner.observe(a, event), Err(Error::InvalidEvent));
        assert_eq!(owner.wait(a, event, 0), Err(Error::InvalidEvent));
        assert_eq!(owner.signal(a, event), Err(Error::InvalidEvent)); assert_eq!(owner, before);
    }
    let before = owner.copy_state();
    assert_eq!(owner.wait(a, 0, 1), Err(Error::InvalidToken)); assert_eq!(owner, before);
    owner.epochs[0] = u64::MAX;
    let before = owner.copy_state();
    assert_eq!(owner.signal(a, 0), Err(Error::CounterExhausted)); assert_eq!(owner, before);
    owner.epochs[0] = 0; owner.events = u64::MAX;
    let before = owner.copy_state();
    assert_eq!(owner.wait(a, 0, 0), Err(Error::CounterExhausted));
    assert_eq!(owner.signal(a, 0), Err(Error::CounterExhausted)); assert_eq!(owner, before);
}

// ====== stale_handle_and_wait_invariants ======

#[test]
fn stale_handle_and_wait_invariants() {
    let (mut owner, a, b) = pair();
    owner.exit(a).unwrap(); owner.collect(a).unwrap();
    let fresh = owner.reserve().unwrap(); owner.publish(fresh).unwrap();
    assert_eq!(owner.select(), Ok(b));
    assert_eq!(owner.signal(a, 0), Err(Error::InvalidHandle));
    owner.yield_task(b).unwrap(); assert_eq!(owner.select(), Ok(fresh));
    owner.wait(fresh, 0, 0).unwrap();
    let mut corrupt = owner.copy_state(); corrupt.waits[fresh.index as usize] = None;
    assert!(!corrupt.validate());
    let mut corrupt = owner.copy_state(); corrupt.epochs[0] = 1; assert!(!corrupt.validate());
    let mut corrupt = owner.copy_state();
    corrupt.waits[b.index as usize] = Some(Token {event: 0, epoch: 0}); assert!(!corrupt.validate());
}

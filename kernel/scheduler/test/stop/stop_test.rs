use super::*;

// ====== ready_owner ======

fn ready_owner() -> (Owner, Handle) {
    let mut owner = Owner::new();
    let handle = owner.reserve().unwrap();
    owner.publish(handle).unwrap();
    (owner, handle)
}

// ====== waiting_stop_and_cleanup ======

#[test]
fn waiting_stop_and_cleanup() {
    let (mut owner, handle) = ready_owner();
    assert_eq!(owner.select(), Ok(handle));
    owner.wait(handle, 0, 0).unwrap();
    assert_eq!(owner.select(), Err(Error::Blocked));
    assert_eq!(owner.request_stop(handle), Ok(1));
    assert!(owner.stop_requested(handle).unwrap());
    assert_eq!(owner.state(handle), Ok(State::Ready));
    assert_eq!(owner.collect(handle), Err(Error::InvalidState));
    let before = owner.copy_state();
    assert_eq!(owner.request_stop(handle), Ok(0));
    assert_eq!(owner, before);
    assert_eq!(owner.signal_pending(1).unwrap().woken, 0);
    assert_eq!(owner.select(), Ok(handle));
    assert_eq!(owner.wait(handle, 0, 1).unwrap().outcome, 5);
    assert_eq!(owner.select(), Ok(handle));
    owner.exit(handle).unwrap();
    assert!(owner.stop_requested(handle).unwrap());
    assert_eq!(owner.request_stop(handle), Err(Error::InvalidState));
    owner.collect(handle).unwrap();
    let next = owner.reserve().unwrap();
    assert_eq!(next.index, handle.index);
    assert_ne!(next.generation, handle.generation);
    owner.publish(next).unwrap();
    assert_eq!(owner.stop_requested(next), Ok(false));
    assert_eq!(owner.request_stop(handle), Err(Error::InvalidHandle));
}

// ====== stop_signal_orderings ======

#[test]
fn stop_signal_orderings() {
    for stop_first in [true, false] {
        let (mut owner, handle) = ready_owner();
        owner.select().unwrap(); owner.wait(handle, 2, 0).unwrap();
        if stop_first {
            assert_eq!(owner.request_stop(handle), Ok(1));
            assert_eq!(owner.signal_pending(4).unwrap().woken, 0);
        } else {
            assert_eq!(owner.signal_pending(4).unwrap().woken, 1);
            assert_eq!(owner.request_stop(handle), Ok(0));
        }
        assert_eq!(owner.ready.len(), 1);
        assert_eq!(owner.select(), Ok(handle));
        assert_eq!(owner.observe(handle, 2).unwrap().epoch, 1);
        assert!(owner.stop_requested(handle).unwrap());
        owner.exit(handle).unwrap(); owner.collect(handle).unwrap();
        assert_eq!(owner.select(), Err(Error::Empty));
    }
}

// ====== stop_rejections_and_counter_rollback ======

#[test]
fn stop_rejections_and_counter_rollback() {
    let (mut owner, handle) = ready_owner();
    for bad in [Handle { index: 8, generation: 1 }, Handle { index: 0, generation: 0 },
        Handle { generation: handle.generation + 1, ..handle }] {
        let before = owner.copy_state();
        assert_eq!(owner.request_stop(bad), Err(Error::InvalidHandle));
        assert_eq!(owner.stop_requested(bad), Err(Error::InvalidHandle));
        assert_eq!(owner, before);
    }
    owner.select().unwrap();
    let before = owner.copy_state();
    assert_eq!(owner.request_stop(handle), Err(Error::Running));
    assert_eq!(owner, before);
    owner.wait(handle, 0, 0).unwrap(); owner.events = u64::MAX;
    let before = owner.copy_state();
    assert_eq!(owner.request_stop(handle), Err(Error::CounterExhausted));
    assert_eq!(owner, before);
    owner.events -= 1;
    owner.request_stop(handle).unwrap();
    let before = owner.copy_state();
    assert_eq!(owner.request_stop(handle), Ok(0));
    assert_eq!(owner, before);
}

// ====== stopped_wait_still_validates_arguments ======

#[test]
fn stopped_wait_still_validates_arguments() {
    let (mut owner, handle) = ready_owner();
    owner.request_stop(handle).unwrap(); owner.select().unwrap();
    let before = owner.copy_state();
    assert_eq!(owner.wait(handle, 8, 0), Err(Error::InvalidEvent));
    assert_eq!(owner.wait(handle, 0, 1), Err(Error::InvalidToken));
    assert_eq!(owner, before);
    assert_eq!(owner.wait(handle, 0, 0).unwrap().outcome, 5);
    assert!(owner.validate());
}

// ====== repeated_waits_then_stop ======

#[test]
fn repeated_waits_then_stop() {
    for _ in 0..1000 {
        let (mut owner, handle) = ready_owner();
        for round in 0..32 {
            assert_eq!(owner.select(), Ok(handle));
            let event = round % 8;
            let epoch = owner.observe(handle, event).unwrap().epoch;
            assert_eq!(owner.wait(handle, event, epoch).unwrap().outcome, 1);
            assert_eq!(owner.signal_pending(1 << event).unwrap().woken, 1);
        }
        owner.select().unwrap();
        let epoch = owner.observe(handle, 0).unwrap().epoch;
        owner.wait(handle, 0, epoch).unwrap();
        assert_eq!(owner.request_stop(handle), Ok(1));
        owner.select().unwrap(); owner.exit(handle).unwrap(); owner.collect(handle).unwrap();
        assert_eq!(owner.select(), Err(Error::Empty));
    }
}

// ====== invalid_stop_flags_fail_closed ======

#[test]
fn invalid_stop_flags_fail_closed() {
    for state in [State::Free, State::Reserved, State::Waiting, State::Retired] {
        let (mut owner, handle) = ready_owner();
        if state == State::Waiting {
            owner.select().unwrap(); owner.wait(handle, 0, 0).unwrap();
        } else { owner.ready.pop(); owner.slots[0].state = state; }
        if state == State::Retired { owner.slots[0].generation = u64::MAX; }
        owner.stops[0] = true;
        assert!(!owner.validate());
        let before = owner.copy_state();
        assert_eq!(owner.request_stop(handle), Err(Error::Corrupt));
        assert_eq!(owner, before);
    }
}

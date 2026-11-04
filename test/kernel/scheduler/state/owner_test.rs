use super::*;

// ====== empty_and_capacity ======

#[test]
fn empty_and_capacity() {
    let mut owner = Owner::new();
    assert_eq!(owner.select(), Err(Error::Empty));
    assert_eq!(owner.current(), Err(Error::Empty));
    for index in 0..CAPACITY {
        let handle = owner.reserve().unwrap();
        assert_eq!(handle, Handle { index: index as u32, generation: 1 });
        owner.publish(handle).unwrap();
    }
    let before = owner.copy_state();
    assert_eq!(owner.reserve(), Err(Error::Capacity));
    assert_eq!(owner, before);
    assert!(owner.validate());
    assert_eq!(owner.snapshot().unwrap(), (8, 16));
}

// ====== lifecycle_and_stale_handles ======

#[test]
fn lifecycle_and_stale_handles() {
    let mut owner = Owner::new();
    let first = owner.reserve().unwrap();
    owner.abort(first).unwrap();
    assert_eq!(owner.abort(first), Err(Error::InvalidState));
    let second = owner.reserve().unwrap();
    assert_eq!(second.generation, 2);
    assert_eq!(owner.publish(first), Err(Error::InvalidHandle));
    owner.publish(second).unwrap();
    assert_eq!(owner.select(), Ok(second));
    assert_eq!(owner.current(), Ok(second));
    assert_eq!(owner.collect(second), Err(Error::Running));
    assert_eq!(owner.reserve(), Err(Error::Running));
    assert_eq!(owner.publish(second), Err(Error::Running));
    owner.yield_task(second).unwrap();
    assert_eq!(owner.select(), Ok(second));
    owner.exit(second).unwrap();
    assert_eq!(owner.yield_task(second), Err(Error::InvalidState));
    assert_eq!(owner.state(second), Ok(State::Finished));
    owner.collect(second).unwrap();
    assert_eq!(owner.collect(second), Err(Error::InvalidState));
    let third = owner.reserve().unwrap();
    assert_eq!(third.generation, 3);
    assert_eq!(owner.state(second), Err(Error::InvalidHandle));
}

// ====== fifo_stress_and_early_exit ======

#[test]
fn fifo_stress_and_early_exit() {
    let mut owner = Owner::new();
    let handles: Vec<_> = (0..CAPACITY).map(|_| {
        let handle = owner.reserve().unwrap();
        owner.publish(handle).unwrap();
        handle
    }).collect();
    for round in 0..20_000 {
        let selected = owner.select().unwrap();
        assert_eq!(selected, handles[round % CAPACITY]);
        owner.yield_task(selected).unwrap();
    }
    for handle in handles {
        assert_eq!(owner.select(), Ok(handle));
        owner.exit(handle).unwrap();
        owner.collect(handle).unwrap();
    }
    assert_eq!(owner.select(), Err(Error::Empty));
    assert_eq!(owner.snapshot().unwrap(), (0, 40_040));
}

// ====== invalid_handles_are_immutable ======

#[test]
fn invalid_handles_are_immutable() {
    let mut owner = Owner::new();
    let handle = owner.reserve().unwrap();
    for invalid in [Handle { index: 8, generation: 1 },
        Handle { index: NONE, generation: u64::MAX },
        Handle { index: 0, generation: 0 }, Handle { index: 0, generation: 2 }] {
        let before = owner.copy_state();
        assert_eq!(owner.publish(invalid), Err(Error::InvalidHandle));
        assert_eq!(owner.abort(invalid), Err(Error::InvalidHandle));
        assert_eq!(owner.collect(invalid), Err(Error::InvalidHandle));
        assert_eq!(owner.yield_task(invalid), Err(Error::InvalidHandle));
        assert_eq!(owner.exit(invalid), Err(Error::InvalidHandle));
        assert_eq!(owner.state(invalid), Err(Error::InvalidHandle));
        assert_eq!(owner, before);
    }
    owner.abort(handle).unwrap();
}

// ====== generation_retirement ======

#[test]
fn generation_retirement() {
    let mut owner = Owner::new();
    owner.slots[0].generation = u64::MAX - 1;
    let last = owner.reserve().unwrap();
    assert_eq!(last.generation, u64::MAX);
    owner.abort(last).unwrap();
    assert_eq!(owner.reserve(), Err(Error::Retired));
    assert_eq!(owner.state(last), Ok(State::Retired));
    assert_eq!(owner.snapshot().unwrap(), (0, 3));
    assert_eq!(owner.reserve().unwrap().index, 1);
    assert_eq!(owner.publish(last), Err(Error::InvalidState));
    owner.slots.fill(Slot { generation: u64::MAX, state: State::Retired, affinity: 0 });
    assert_eq!(owner.reserve(), Err(Error::Capacity));
    assert!(owner.validate());
}

// ====== counter_exhaustion_rolls_back ======

#[test]
fn counter_exhaustion_rolls_back() {
    let mut owner = Owner::new();
    let handle = owner.reserve().unwrap();
    owner.events = u64::MAX;
    let before = owner.copy_state();
    assert_eq!(owner.publish(handle), Err(Error::CounterExhausted));
    assert_eq!(owner.abort(handle), Err(Error::CounterExhausted));
    assert_eq!(owner, before);
    assert_eq!(owner.snapshot(), Ok((0, u64::MAX)));
    owner.slots[0] = Slot { generation: u64::MAX, state: State::Free, affinity: 0 };
    let before = owner.copy_state();
    assert_eq!(owner.reserve(), Err(Error::CounterExhausted));
    assert_eq!(owner, before);
}

// ====== counter_exhaustion_all_mutations ======

#[test]
fn counter_exhaustion_all_mutations() {
    for operation in 0..7 {
        let mut owner = Owner::new();
        let handle = owner.reserve().unwrap();
        if operation >= 3 { owner.publish(handle).unwrap(); }
        if operation >= 4 { owner.select().unwrap(); }
        if operation == 6 { owner.exit(handle).unwrap(); }
        owner.events = u64::MAX;
        let before = owner.copy_state();
        let result = match operation {
            0 => owner.reserve().map(|_| ()),
            1 => owner.publish(handle),
            2 => owner.abort(handle),
            3 => owner.select().map(|_| ()),
            4 => owner.yield_task(handle),
            5 => owner.exit(handle),
            _ => owner.collect(handle),
        };
        assert_eq!(result, Err(Error::CounterExhausted));
        assert_eq!(owner, before);
    }
}

// ====== corrupted_owner_fail_closed ======

#[test]
fn corrupted_owner_fail_closed() {
    for variant in 0..5 {
        let mut owner = Owner::new();
        let handle = owner.reserve().unwrap();
        match variant {
            0 => owner.slots[0].state = State::Ready,
            1 => owner.current[0] = Some(handle),
            2 => owner.slots[0].generation = 0,
            3 => owner.slots[0].state = State::Retired,
            _ => { owner.ready.push(0); }
        }
        assert!(!owner.validate());
        let before = owner.copy_state();
        assert_eq!(owner.reserve(), Err(Error::Corrupt));
        assert_eq!(owner.publish(handle), Err(Error::Corrupt));
        assert_eq!(owner.abort(handle), Err(Error::Corrupt));
        assert_eq!(owner.select(), Err(Error::Corrupt));
        assert_eq!(owner.yield_task(handle), Err(Error::Corrupt));
        assert_eq!(owner.exit(handle), Err(Error::Corrupt));
        assert_eq!(owner.collect(handle), Err(Error::Corrupt));
        assert_eq!(owner.snapshot(), Err(Error::Corrupt));
        assert_eq!(owner, before);
    }
}

// ====== native_state_budget ======

#[test]
fn native_state_budget() {
    let bytes = core::mem::size_of::<Owner>();
    assert!(bytes <= 4096);
    println!("Rust owner metadata: {bytes} bytes (no heap)");
}

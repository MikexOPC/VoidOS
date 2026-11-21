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
    println!("Rust owner metadata: {bytes} bytes (storage is external)");
}

// ====== capacity_beyond_eight_lifecycle ======

#[test]
fn capacity_beyond_eight_lifecycle() {
    for capacity in [9usize, 64, 256] {
        let mut owner = Owner::with_capacity(capacity).unwrap();
        let handles: Vec<_> = (0..capacity).map(|index| {
            let handle = owner.reserve().unwrap();
            assert_eq!(handle.index as usize, index);
            owner.publish(handle).unwrap();
            handle
        }).collect();
        assert_eq!(owner.reserve(), Err(Error::Capacity));
        assert_eq!(owner.snapshot().unwrap().0 as usize, capacity);
        for &handle in &handles[..capacity - 1] {
            assert_eq!(owner.select(), Ok(handle));
            assert_eq!(owner.wait(handle, 0, 0).unwrap().outcome, 1);
        }
        let last = handles[capacity - 1];
        assert_eq!(owner.select(), Ok(last));
        assert_eq!(owner.signal(last, 0).unwrap().woken as usize, capacity - 1);
        owner.exit(last).unwrap();
        let stopped = handles[capacity / 2];
        assert_eq!(owner.request_stop(stopped), Ok(0));
        assert_eq!(owner.stop_requested(stopped), Ok(true));
        assert!(owner.validate());
        for &handle in &handles[..capacity - 1] {
            assert_eq!(owner.select(), Ok(handle));
            owner.exit(handle).unwrap();
            owner.collect(handle).unwrap();
        }
        owner.collect(last).unwrap();
        assert_eq!(owner.select(), Err(Error::Empty));
        assert_eq!(owner.snapshot().unwrap().0, 0);
        for index in 0..capacity {
            let again = owner.reserve().unwrap();
            assert_eq!((again.index as usize, again.generation), (index, 2));
            owner.publish(again).unwrap();
            assert_eq!(owner.stop_requested(again), Ok(false));
        }
    }
}

// ====== capacity_lot_uses_tables_beyond_eight ======

#[test]
fn capacity_lot_uses_tables_beyond_eight() {
    let mut owner = Owner::with_capacity(64).unwrap();
    owner.configure(4, 1).unwrap();
    let fillers: Vec<_> = (0..12).map(|_| owner.reserve_on(0).unwrap()).collect();
    let tasks: Vec<Handle> = (0..4).map(|i| {
        let handle = owner.reserve_on(0).unwrap();
        assert!(handle.index >= 12);
        owner.set_affinity_on(0, handle, 1 << (i / 2 + 1)).unwrap();
        owner.publish_on(0, handle).unwrap();
        handle
    }).collect();
    for filler in fillers { owner.abort_on(0, filler).unwrap(); }
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    assert!(owner.validate());
    for cpu in [1u32, 2] {
        owner.lot_transition(cpu, 0x1000, 1, 1).unwrap();
        for _ in 0..2 {
            let handle = owner.select_on(cpu).unwrap();
            assert!(handle.index >= 12);
            owner.exit_on(cpu, handle).unwrap();
        }
        owner.lot_transition(cpu, 0x1000, 1, 2).unwrap();
        owner.lot_transition(cpu, 0x1000, 1, 3).unwrap();
    }
    for handle in tasks { owner.collect_on(0, handle).unwrap(); }
    owner.lot_transition(0, 0x1000, 1, 6).unwrap();
    assert!(owner.validate());
    assert!(owner.reserve_on(0).is_ok());
}

// ====== storage_validation_rejects_bad_inputs ======

#[test]
fn storage_validation_rejects_bad_inputs() {
    for capacity in [0usize, 1, 7, 257, 1024, usize::MAX] {
        assert_eq!(storage_bytes(capacity), None);
        assert_eq!(Owner::with_capacity(capacity), Err(Error::BadArgument));
    }
    let mut last = 0;
    for capacity in MIN_CAPACITY..=MAX_CAPACITY {
        let bytes = storage_bytes(capacity).unwrap();
        assert!(bytes > last && bytes.is_multiple_of(STORAGE_ALIGN));
        last = bytes;
        assert!(Owner::with_capacity(capacity).unwrap().validate());
    }
    let capacity = 16;
    let bytes = storage_bytes(capacity).unwrap();
    let mut words = vec![u64::MAX; bytes / 8 + 1];
    let base = words.as_mut_ptr().cast::<u8>();
    // SAFETY: `words` outlives every call and spans `bytes + 8` bytes.
    unsafe {
        let null = core::ptr::null_mut();
        assert_eq!(Owner::from_storage(null, bytes, capacity).err(), Some(Error::BadArgument));
        assert_eq!(Owner::from_storage(base.add(1), bytes, capacity).err(), Some(Error::BadArgument));
        assert_eq!(Owner::from_storage(base.add(4), bytes, capacity).err(), Some(Error::BadArgument));
        assert_eq!(Owner::from_storage(base, bytes - 8, capacity).err(), Some(Error::BadArgument));
        assert_eq!(Owner::from_storage(base, 0, capacity).err(), Some(Error::BadArgument));
        let mut owner = Owner::from_storage(base, bytes + 8, capacity).unwrap();
        assert!(owner.validate());
        assert_eq!(owner.snapshot(), Ok((0, 0)));
        assert_eq!(owner.reserve().unwrap().index, 0);
    }
}

// ====== transaction_rollback_preserves_active_bank ======

#[test]
fn transaction_rollback_preserves_active_bank() {
    let mut owner = Owner::with_capacity(64).unwrap();
    for _ in 0..40 {
        let handle = owner.reserve().unwrap();
        owner.publish(handle).unwrap();
    }
    let before = owner.copy_state();
    let bank = owner.active_bank;
    let failed: Result<(), Error> = owner.transaction(|next| {
        next.slots[5].state = State::Free;
        next.slots[40].generation = 7;
        next.stops[9] = true;
        assert!(next.ready.push(50));
        next.events += 5;
        Err(Error::InvalidState)
    });
    assert_eq!(failed, Err(Error::InvalidState));
    assert_eq!(owner, before);
    assert_eq!(owner.active_bank, bank);
    assert!(owner.validate());
    owner.reserve().unwrap();
    assert_eq!(owner.active_bank, bank ^ 1);
    owner.select().unwrap();
    assert_eq!(owner.active_bank, bank);
    assert!(owner.validate());
    owner.active_bank ^= 1;
    assert!(!owner.validate());
    assert_eq!(owner.reserve(), Err(Error::Corrupt));
    owner.active_bank ^= 1;
    assert!(owner.validate());
}
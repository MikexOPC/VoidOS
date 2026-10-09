use super::*;
use std::collections::VecDeque;

#[derive(Clone)]
struct Model {
    slots: [(State, u64); CAPACITY],
    order: VecDeque<usize>,
    active: Option<usize>,
    events: u64,
}

// ====== independent_model ======

impl Model {
    fn new() -> Self {
        Self { slots: [(State::Free, 0); CAPACITY], order: VecDeque::new(),
            active: None, events: 0 }
    }

    // Oracle uses a dynamic deque and direct state updates, not a ring/candidate.
    fn apply(&mut self, operation: u32, handle: Handle) -> Result<Handle, Error> {
        if matches!(operation, 0 | 1 | 2 | 3 | 6) && self.active.is_some() {
            return Err(Error::Running);
        }
        if operation == 0 {
            let index = self.slots.iter().position(|s| s.0 == State::Free)
                .ok_or(Error::Capacity)?;
            if self.slots[index].1 == u64::MAX {
                self.slots[index].0 = State::Retired;
                self.events += 1;
                return Err(Error::Retired);
            }
            self.slots[index].1 += 1;
            self.slots[index].0 = State::Reserved;
            self.events += 1;
            return Ok(Handle { index: index as u32, generation: self.slots[index].1 });
        }
        if operation == 3 {
            let index = self.order.pop_front().ok_or(Error::Empty)?;
            self.slots[index].0 = State::Running;
            self.active = Some(index);
            self.events += 1;
            return Ok(Handle { index: index as u32, generation: self.slots[index].1 });
        }
        let index = handle.index as usize;
        if index >= CAPACITY || handle.generation == 0 || self.slots[index].1 != handle.generation {
            return Err(Error::InvalidHandle);
        }
        let expected = match operation {
            1 | 2 => State::Reserved,
            4 | 5 => State::Running,
            6 => State::Finished,
            7 => return Ok(handle),
            _ => unreachable!(),
        };
        if self.slots[index].0 != expected { return Err(Error::InvalidState); }
        match operation {
            1 => { self.slots[index].0 = State::Ready; self.order.push_back(index); }
            2 | 6 => self.slots[index].0 = State::Free,
            4 => {
                self.slots[index].0 = State::Ready;
                self.order.push_back(index);
                self.active = None;
            }
            5 => { self.slots[index].0 = State::Finished; self.active = None; }
            _ => unreachable!(),
        }
        self.events += 1;
        Ok(handle)
    }
}

// ====== actual_command ======

fn actual(owner: &mut Owner, op: u32, handle: Handle) -> Result<Handle, Error> {
    match op {
        0 => owner.reserve(),
        1 => owner.publish(handle).map(|()| handle),
        2 => owner.abort(handle).map(|()| handle),
        3 => owner.select(),
        4 => owner.yield_task(handle).map(|()| handle),
        5 => owner.exit(handle).map(|()| handle),
        6 => owner.collect(handle).map(|()| handle),
        7 => owner.state(handle).map(|_| handle),
        _ => unreachable!(),
    }
}

// ====== compare_model_snapshot ======

fn compare(owner: &Owner, model: &Model) {
    assert!(owner.validate());
    assert_eq!(owner.snapshot(), Ok((model.order.len() as u32, model.events)));
    assert_eq!(owner.current(), model.active.map(|index| Handle {
        index: index as u32, generation: model.slots[index].1,
    }).ok_or(Error::Empty));
    for (index, (state, generation)) in model.slots.iter().enumerate() {
        assert_eq!(owner.slots[index].state, *state);
        assert_eq!(owner.slots[index].generation, *generation);
    }
}

// ====== deterministic_trace_oracle ======

#[test]
fn deterministic_trace_oracle() {
    for seed in 1u64..=64 {
        let mut rng = seed;
        let mut owner = Owner::new();
        let mut model = Model::new();
        for step in 0..5_000 {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            let op = ((rng >> 8) % 8) as u32;
            let index = if step % 4 == 0 { model.active.unwrap_or((rng % 8) as usize) }
                else { (rng % 8) as usize };
            let mut handle = Handle { index: index as u32, generation: model.slots[index].1 };
            if rng & 31 == 0 { handle.generation = 0; }
            if rng & 127 == 1 { handle.index = NONE; }
            let before = owner.copy_state();
            let expected = model.apply(op, handle);
            assert_eq!(actual(&mut owner, op, handle), expected, "seed {seed} step {step}");
            if expected.is_err() && expected != Err(Error::Retired) { assert_eq!(owner, before); }
            compare(&owner, &model);
        }
    }
    println!("Oracle: 64 fixed seeds, 320000 commands compared");
}

// ====== full_state_operation_matrix ======

#[test]
fn full_state_operation_matrix() {
    for state in [State::Free, State::Reserved, State::Ready, State::Running,
        State::Finished, State::Retired] {
        for operation in 0..8 {
            let mut owner = Owner::new();
            let mut model = Model::new();
            owner.slots[0] = Slot { state, generation: 1 };
            model.slots[0] = (state, 1);
            if state == State::Retired {
                owner.slots[0].generation = u64::MAX;
                model.slots[0].1 = u64::MAX;
            }
            if state == State::Ready { owner.ready.push(0); model.order.push_back(0); }
            let handle = Handle { index: 0, generation: model.slots[0].1 };
            if state == State::Running { owner.current = Some(handle); model.active = Some(0); }
            assert!(owner.validate());
            assert_eq!(actual(&mut owner, operation, handle), model.apply(operation, handle));
            compare(&owner, &model);
        }
    }
}

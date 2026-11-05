use super::super::{Error, Handle, Owner, State};

// ====== prepared ======

fn prepared() -> (Owner, [Handle; 4]) {
    let mut owner = Owner::new();
    owner.configure(4, 1).unwrap();
    let tasks = core::array::from_fn(|i| {
        let h = owner.reserve_on(0).unwrap();
        owner.set_affinity_on(0, h, 1 << (i / 2 + 1)).unwrap();
        owner.publish_on(0, h).unwrap();
        h
    });
    (owner, tasks)
}

// ====== finish ======

fn finish(owner: &mut Owner, cpu: u32) {
    owner.lot_transition(cpu, 0x1000, 1, 1).unwrap();
    for _ in 0..2 {
        let h = owner.select_on(cpu).unwrap();
        owner.exit_on(cpu, h).unwrap();
    }
    owner.lot_transition(cpu, 0x1000, 1, 2).unwrap();
    owner.lot_transition(cpu, 0x1000, 1, 3).unwrap();
}

// ====== collection_requires_both_quiescent ======

#[test]
fn collection_requires_both_quiescent() {
    let (mut owner, tasks) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    assert_eq!(owner.select_on(1), Err(Error::InvalidState));
    finish(&mut owner, 1);
    assert_eq!(owner.select_on(0), Err(Error::InvalidState));
    assert_eq!(owner.collect_on(0, tasks[0]), Err(Error::InvalidState));
    assert_eq!(owner.lot_transition(0, 0x1000, 1, 6), Err(Error::InvalidState));
    let snapshot = owner.lot_snapshot(0, 0x1000, 1).unwrap();
    assert_eq!((snapshot.entered, snapshot.completed, snapshot.quiesced), (2, 2, 2));
    finish(&mut owner, 2);
    assert_eq!(owner.collect_on(1, tasks[0]), Err(Error::InvalidState));
    for h in tasks { owner.collect_on(0, h).unwrap(); }
    assert!(owner.validate());
    assert_eq!(owner.reserve_on(0), Err(Error::InvalidState));
    owner.lot_transition(0, 0x1000, 1, 6).unwrap();
    assert!(owner.reserve_on(0).is_ok());
}

// ====== premature_and_duplicate_transitions ======

#[test]
fn premature_and_duplicate_transitions() {
    let (mut owner, _) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    for op in [2, 3, 6] { assert!(owner.lot_transition(1, 0x1000, 1, op).is_err()); }
    owner.lot_transition(1, 0x1000, 1, 1).unwrap();
    assert_eq!(owner.lot_transition(1, 0x1000, 1, 1), Err(Error::InvalidState));
    assert_eq!(owner.lot_transition(1, 0x1000, 1, 2), Err(Error::InvalidState));
    let h = owner.select_on(1).unwrap();
    assert_eq!(owner.lot_transition(1, 0x1000, 1, 2), Err(Error::Running));
    owner.exit_on(1, h).unwrap();
    let h = owner.select_on(1).unwrap();
    owner.exit_on(1, h).unwrap();
    owner.lot_transition(1, 0x1000, 1, 2).unwrap();
    assert_eq!(owner.lot_transition(1, 0x1000, 1, 2), Err(Error::InvalidState));
    owner.lot_transition(1, 0x1000, 1, 3).unwrap();
    assert_eq!(owner.lot_transition(1, 0x1000, 1, 3), Err(Error::InvalidState));
}

// ====== bad_identity_is_transactional ======

#[test]
fn bad_identity_is_transactional() {
    let (mut owner, _) = prepared();
    for (cpu, group, token, mask) in [(1, 1, 1, 6), (0, 0, 1, 6), (0, 1, 0, 6),
        (0, 1, 1, 3), (0, 1, 1, 2), (0, 1, 1, 14), (0, 1, 1, 18)] {
        let before = owner.copy_state();
        assert!(owner.lot_open(cpu, group, token, mask).is_err());
        assert_eq!(owner, before);
    }
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    for (cpu, group, token, op) in [(1, 2, 1, 1), (1, 0x1000, 2, 1),
        (3, 0x1000, 1, 1), (64, 0x1000, 1, 1), (0, 0x1000, 1, 1)] {
        let before = owner.copy_state();
        assert!(owner.lot_transition(cpu, group, token, op).is_err());
        assert_eq!(owner, before);
    }
    assert!(owner.lot_snapshot(3, 0x1000, 1).is_err());
    assert!(owner.lot_snapshot(0, 0x1000, 2).is_err());
}

// ====== terminal_failure_prevents_release ======

#[test]
fn terminal_failure_prevents_release() {
    let (mut owner, tasks) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    finish(&mut owner, 1);
    finish(&mut owner, 2);
    owner.lot_transition(0, 0x1000, 1, 5).unwrap();
    owner.lot_transition(0, 0x1000, 1, 5).unwrap();
    assert!(owner.lot_snapshot(0, 0x1000, 1).unwrap().failed);
    for h in tasks { assert_eq!(owner.collect_on(0, h), Err(Error::InvalidState)); }
    assert_eq!(owner.lot_transition(0, 0x1000, 1, 6), Err(Error::InvalidState));
    assert_eq!(owner.select_on(1), Err(Error::InvalidState));
    assert!(owner.validate());
}

// ====== missing_task_and_mutations_rejected ======

#[test]
fn missing_task_and_mutations_rejected() {
    let (mut owner, tasks) = prepared();
    let h = owner.select_on(1).unwrap();
    assert_eq!(owner.lot_open(0, 0x1000, 1, 6), Err(Error::InvalidState));
    owner.exit_on(1, h).unwrap();
    assert_eq!(owner.lot_open(0, 0x1000, 1, 6), Err(Error::InvalidState));
    owner.collect_on(0, h).unwrap();
    assert_eq!(owner.lot_open(0, 0x1000, 1, 6), Err(Error::InvalidState));
    assert_eq!(owner.state(tasks[0]), Ok(State::Free));
    let (mut owner, tasks) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    assert_eq!(owner.configure(4, 2), Err(Error::InvalidState));
    assert_eq!(owner.abort_on(0, tasks[0]), Err(Error::InvalidState));
    assert_eq!(owner.publish_on(0, tasks[0]), Err(Error::InvalidState));
}

// ====== repeated_lots_require_new_tokens ======

#[test]
fn repeated_lots_require_new_tokens() {
    let (mut owner, tasks) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    finish(&mut owner, 1);
    finish(&mut owner, 2);
    for h in tasks { owner.collect_on(0, h).unwrap(); }
    owner.lot_transition(0, 0x1000, 1, 6).unwrap();
    for i in 0..4 {
        let h = owner.reserve_on(0).unwrap();
        owner.set_affinity_on(0, h, 1 << (i / 2 + 1)).unwrap();
        owner.publish_on(0, h).unwrap();
    }
    assert_eq!(owner.lot_open(0, 0x2000, 1, 6), Err(Error::InvalidState));
    owner.lot_open(0, 0x2000, 2, 6).unwrap();
    assert_eq!(owner.lot_transition(1, 0x1000, 1, 1), Err(Error::InvalidToken));
}

// ====== failure_with_running_peer_retains_resources ======

#[test]
fn failure_with_running_peer_retains_resources() {
    let (mut owner, tasks) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    owner.lot_transition(1, 0x1000, 1, 1).unwrap();
    let h = owner.select_on(1).unwrap();
    assert_eq!(owner.lot_transition(3, 0x1000, 1, 5), Err(Error::InvalidCpu));
    owner.lot_transition(1, 0x1000, 1, 5).unwrap();
    assert_eq!(owner.current_on(1), Ok(h));
    assert_eq!(owner.state(tasks[0]), Ok(State::Running));
    assert!(owner.lot_snapshot(1, 0x1000, 1).unwrap().failed);
    assert_eq!(owner.collect_on(0, h), Err(Error::InvalidState));
    assert_eq!(owner.lot_transition(2, 0x1000, 1, 1), Err(Error::InvalidState));
}

// ====== cpu_63_and_token_max_do_not_wrap ======

#[test]
fn cpu_63_and_token_max_do_not_wrap() {
    let mut owner = Owner::new();
    owner.configure(64, 1).unwrap();
    for cpu in [1, 1, 63, 63] {
        let h = owner.reserve_on(0).unwrap();
        owner.set_affinity_on(0, h, 1 << cpu).unwrap();
        owner.publish_on(0, h).unwrap();
    }
    let mask = (1 << 63) | 2;
    owner.lot_open(0, u64::MAX, u64::MAX, mask).unwrap();
    for cpu in [1, 63] {
        owner.lot_transition(cpu, u64::MAX, u64::MAX, 1).unwrap();
        for _ in 0..2 {
            let h = owner.select_on(cpu).unwrap();
            owner.exit_on(cpu, h).unwrap();
        }
        owner.lot_transition(cpu, u64::MAX, u64::MAX, 2).unwrap();
        owner.lot_transition(cpu, u64::MAX, u64::MAX, 3).unwrap();
    }
    assert_eq!(owner.lot_snapshot(0, u64::MAX, u64::MAX).unwrap().quiesced, mask);
    for i in 0..4 { owner.collect_on(0, Handle { index: i, generation: 1 }).unwrap(); }
    owner.lot_transition(0, u64::MAX, u64::MAX, 6).unwrap();
    assert_eq!(owner.lot_open(0, 1, 0, mask), Err(Error::InvalidState));
    assert_eq!(owner.lot_open(0, 1, u64::MAX, mask), Err(Error::InvalidState));
}

// ====== corrupted_lot_fails_closed ======

#[test]
fn corrupted_lot_fails_closed() {
    let (mut owner, _) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    for kind in 0..7 {
        let mut broken = owner.copy_state();
        let lot = broken.lot.as_mut().unwrap();
        match kind {
            0 => lot.quiesced = 2,
            1 => lot.completed = 2,
            2 => lot.entered = 8,
            3 => lot.tasks[0].as_mut().unwrap().generation += 1,
            4 => lot.cpus[0] = 64,
            5 => lot.tasks[0] = None,
            _ => lot.token += 1,
        }
        assert!(!broken.validate());
        assert_eq!(broken.lot_snapshot(0, 0x1000, 1), Err(Error::Corrupt));
        assert_eq!(broken.lot_transition(0, 0x1000, 1, 5), Err(Error::Corrupt));
    }
}

// ====== failure_after_partial_collection_keeps_remaining_slots ======

#[test]
fn failure_after_partial_collection_keeps_remaining_slots() {
    let (mut owner, tasks) = prepared();
    owner.lot_open(0, 0x1000, 1, 6).unwrap();
    finish(&mut owner, 1);
    finish(&mut owner, 2);
    owner.collect_on(0, tasks[0]).unwrap();
    owner.lot_transition(0, 0x1000, 1, 5).unwrap();
    assert_eq!(owner.state(tasks[0]), Ok(State::Free));
    assert_eq!(owner.state(tasks[1]), Ok(State::Finished));
    assert_eq!(owner.collect_on(0, tasks[1]), Err(Error::InvalidState));
    assert_eq!(owner.reserve_on(0), Err(Error::InvalidState));
    assert!(owner.validate());
}


// ====== two_to_four_participants_require_joint_quiescence ======

#[test]
fn two_to_four_participants_require_joint_quiescence() {
    for aps in 2..=4u32 {
        let mut owner = Owner::new();
        owner.configure(aps + 1, 1).unwrap();
        let mask = (1u64 << (aps + 1)) - 2;
        let mut tasks = Vec::new();
        for cpu in 1..=aps {
            for _ in 0..2 {
                let h = owner.reserve_on(0).unwrap();
                owner.set_affinity_on(0, h, 1 << cpu).unwrap();
                owner.publish_on(0, h).unwrap();
                tasks.push(h);
            }
        }
        owner.lot_open(0, 0x1000, 1, mask).unwrap();
        for cpu in 1..=aps { owner.lot_transition(cpu, 0x1000, 1, 1).unwrap(); }
        let active: Vec<_> = (1..=aps).map(|cpu| owner.select_on(cpu).unwrap()).collect();
        for (at, h) in active.iter().enumerate() {
            let cpu = at as u32 + 1;
            assert_eq!(owner.tick_on(cpu), Ok(true));
            owner.preempt_on(cpu, *h).unwrap();
        }
        for cpu in 1..=aps {
            for _ in 0..2 {
                let h = owner.select_on(cpu).unwrap();
                owner.exit_on(cpu, h).unwrap();
            }
            owner.lot_transition(cpu, 0x1000, 1, 2).unwrap();
            if cpu < aps { owner.lot_transition(cpu, 0x1000, 1, 3).unwrap(); }
        }
        assert_eq!(owner.lot_snapshot(0, 0x1000, 1).unwrap().completed, mask);
        for h in &tasks { assert_eq!(owner.collect_on(0, *h), Err(Error::InvalidState)); }
        owner.lot_transition(aps, 0x1000, 1, 3).unwrap();
        for h in tasks { owner.collect_on(0, h).unwrap(); }
        owner.lot_transition(0, 0x1000, 1, 6).unwrap();
        assert!(owner.validate());
    }
}

// ====== participant_capacity_and_bsp_membership_are_rejected ======

#[test]
fn participant_capacity_and_bsp_membership_are_rejected() {
    let mut owner = Owner::new();
    owner.configure(6, 1).unwrap();
    for mask in [0, 2, 7, 0x3e, 1 << 63] {
        let before = owner.copy_state();
        assert!(owner.lot_open(0, 0x1000, 1, mask).is_err());
        assert_eq!(owner, before);
    }
}

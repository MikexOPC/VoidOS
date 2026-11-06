use super::super::{Error, Handle, Owner, State, CAPACITY};
use super::{CPUS, SLICE_MAX};

// ====== published ======

fn published(owner: &mut Owner, cpu: u32, count: usize) -> Vec<Handle> {
    (0..count).map(|_| {
        let handle = owner.reserve_on(cpu).unwrap();
        owner.publish_on(cpu, handle).unwrap();
        handle
    }).collect()
}

// ====== configure_limits ======

#[test]
fn configure_limits() {
    let mut owner = Owner::new();
    assert_eq!(owner.cpus(), 1);
    for (cpus, slice) in [(0, 1), (CPUS as u32 + 1, 1), (4, 0), (4, SLICE_MAX + 1)] {
        assert_eq!(owner.configure(cpus, slice), Err(Error::InvalidCpu));
    }
    assert_eq!(owner.configure(CPUS as u32, SLICE_MAX), Ok(()));
    assert_eq!(owner.configure(4, 3), Ok(()));
    let handle = owner.reserve_on(0).unwrap();
    assert_eq!(owner.affinity(handle), Ok(0b1111));
    assert_eq!(owner.configure(2, 3), Err(Error::Running));
    owner.abort_on(0, handle).unwrap();
    assert_eq!(owner.configure(2, 3), Ok(()));
    assert_eq!(owner.select_on(2), Err(Error::InvalidCpu));
    assert!(owner.validate());
}

// ====== cpus_never_share_a_task ======

#[test]
fn cpus_never_share_a_task() {
    let mut owner = Owner::new();
    owner.configure(4, 2).unwrap();
    let tasks = published(&mut owner, 0, 3);
    let picked: Vec<Handle> = (0..3).map(|cpu| owner.select_on(cpu).unwrap()).collect();
    assert_eq!(picked, tasks);
    assert_eq!(owner.select_on(3), Err(Error::Blocked));
    assert_eq!(owner.select_on(0), Err(Error::Running));
    assert_eq!(owner.yield_on(1, picked[0]), Err(Error::InvalidState));
    owner.yield_on(0, picked[0]).unwrap();
    assert_eq!(owner.select_on(3), Ok(picked[0]));
    for (cpu, handle) in [(3, picked[0]), (1, picked[1]), (2, picked[2])] {
        owner.exit_on(cpu, handle).unwrap();
        owner.collect_on(cpu, handle).unwrap();
    }
    assert_eq!(owner.select_on(0), Err(Error::Empty));
    assert!(owner.validate());
}

// ====== affinity_is_respected ======

#[test]
fn affinity_is_respected() {
    let mut owner = Owner::new();
    owner.configure(4, 1).unwrap();
    let pinned = owner.reserve_on(0).unwrap();
    assert_eq!(owner.set_affinity_on(0, pinned, 0), Err(Error::InvalidCpu));
    assert_eq!(owner.set_affinity_on(0, pinned, 1 << 4), Err(Error::InvalidCpu));
    owner.set_affinity_on(0, pinned, 1 << 2).unwrap();
    owner.publish_on(0, pinned).unwrap();
    assert_eq!(owner.set_affinity_on(0, pinned, 1), Err(Error::InvalidState));
    let free = published(&mut owner, 0, 1)[0];
    assert_eq!(owner.select_on(0), Ok(free));
    assert_eq!(owner.select_on(1), Err(Error::Blocked));
    assert_eq!(owner.select_on(2), Ok(pinned));
    assert!(owner.validate());
}

// ====== foreign_affinity_is_empty ======

#[test]
fn foreign_affinity_is_empty() {
    let mut owner = Owner::new();
    owner.configure(2, 1).unwrap();
    let ap = owner.reserve_on(0).unwrap();
    owner.set_affinity_on(0, ap, 1 << 1).unwrap();
    owner.publish_on(0, ap).unwrap();
    assert_eq!(owner.select_on(0), Err(Error::Empty));
    assert_eq!(owner.select_on(1), Ok(ap));
    assert_eq!(owner.select_on(0), Err(Error::Empty));
    let bsp = owner.reserve_on(0).unwrap();
    owner.set_affinity_on(0, bsp, 1).unwrap();
    owner.publish_on(0, bsp).unwrap();
    assert_eq!(owner.select_on(0), Ok(bsp));
    owner.yield_on(0, bsp).unwrap();
    owner.yield_on(1, ap).unwrap();
    assert_eq!(owner.select_on(0), Ok(bsp));
    assert_eq!(owner.select_on(0), Err(Error::Running));
    assert_eq!(owner.select_on(1), Ok(ap));
    owner.exit_on(0, bsp).unwrap();
    owner.collect_on(0, bsp).unwrap();
    assert_eq!(owner.select_on(0), Err(Error::Empty));
    owner.exit_on(1, ap).unwrap();
    owner.collect_on(1, ap).unwrap();
    assert_eq!(owner.select_on(1), Err(Error::Empty));
    assert!(owner.validate());
}

// ====== tick_requests_preemption ======

#[test]
fn tick_requests_preemption() {
    let mut owner = Owner::new();
    owner.configure(2, 3).unwrap();
    let tasks = published(&mut owner, 0, 1);
    assert_eq!(owner.tick_on(0), Ok(false));
    let running = owner.select_on(0).unwrap();
    for _ in 0..5 { assert_eq!(owner.tick_on(0), Ok(false)); }
    let waiting = published(&mut owner, 1, 1)[0];
    assert_eq!(owner.tick_on(0), Ok(true));
    owner.preempt_on(0, running).unwrap();
    assert_eq!(owner.select_on(0), Ok(waiting));
    assert_eq!(owner.select_on(1), Ok(tasks[0]));
    for _ in 0..2 { assert_eq!(owner.tick_on(0), Ok(false)); }
    assert_eq!(owner.tick_on(0), Ok(false));
    assert_eq!(owner.tick_on(9), Err(Error::InvalidCpu));
    assert!(owner.validate());
}

// ====== wait_frees_only_its_cpu ======

#[test]
fn wait_frees_only_its_cpu() {
    let mut owner = Owner::new();
    owner.configure(2, 1).unwrap();
    let tasks = published(&mut owner, 0, 2);
    let first = owner.select_on(0).unwrap();
    let second = owner.select_on(1).unwrap();
    assert_eq!(owner.wait_on(1, first, 0, 0), Err(Error::InvalidState));
    assert_eq!(owner.wait_on(0, first, 0, 0).unwrap().outcome, 1);
    assert_eq!(owner.current_on(0), Err(Error::Empty));
    assert_eq!(owner.current_on(1), Ok(second));
    assert_eq!(owner.signal_on(1, second, 0).unwrap().woken, 1);
    assert_eq!(owner.state(tasks[0]), Ok(State::Ready));
    assert_eq!(owner.select_on(0), Ok(first));
    assert!(owner.validate());
}

// ====== random_schedule_keeps_invariants ======

#[test]
fn random_schedule_keeps_invariants() {
    let mut owner = Owner::new();
    owner.configure(4, 2).unwrap();
    let mut seed = 0x2545_f491_4f6c_dd1du64;
    let mut handles: Vec<Option<Handle>> = vec![None; CAPACITY];
    for _ in 0..200_000 {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        let cpu = (seed % 4) as u32;
        let slot = ((seed >> 8) % CAPACITY as u64) as usize;
        match (seed >> 16) % 7 {
            0 => if let Ok(handle) = owner.reserve_on(cpu) {
                if (seed >> 24) & 1 == 1 { let _ = owner.set_affinity_on(cpu, handle, 1 << ((seed >> 25) % 4)); }
                owner.publish_on(cpu, handle).unwrap();
                handles[handle.index as usize] = Some(handle);
            },
            1 => { let _ = owner.select_on(cpu); }
            2 => if let Ok(handle) = owner.current_on(cpu) { owner.yield_on(cpu, handle).unwrap(); },
            3 => if let Ok(true) = owner.tick_on(cpu) {
                let handle = owner.current_on(cpu).unwrap();
                owner.preempt_on(cpu, handle).unwrap();
            },
            4 => if let Ok(handle) = owner.current_on(cpu) {
                owner.exit_on(cpu, handle).unwrap();
                owner.collect_on((cpu + 1) % 4, handle).unwrap_or_else(|_| owner.collect_on(cpu, handle).unwrap());
                handles[handle.index as usize] = None;
            },
            5 => if let Ok(handle) = owner.current_on(cpu) { let _ = owner.wait_on(cpu, handle, 0, 0); },
            _ => if let Ok(handle) = owner.current_on(cpu) { let _ = owner.signal_on(cpu, handle, 0); },
        }
        assert!(owner.validate());
        let running: Vec<Handle> = (0..4).filter_map(|cpu| owner.current_on(cpu).ok()).collect();
        for (i, a) in running.iter().enumerate() {
            assert!(running[i + 1..].iter().all(|b| b.index != a.index));
            assert_eq!(owner.state(*a), Ok(State::Running));
        }
        let _ = slot;
    }
}

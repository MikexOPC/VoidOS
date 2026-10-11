use super::*;
use crate::CAPACITY;
use std::collections::VecDeque;

// ====== event_lifecycle_oracle ======

#[test]
fn event_lifecycle_oracle() {
    let mut seed = 0xA83F1964_u64;
    let mut commands = 0;
    for _trace in 0..32 {
        let mut owner = Owner::new();
        let mut epochs = [0; EVENT_CAPACITY];
        for _cycle in 0..128 {
            let handles: Vec<_> = (0..CAPACITY).map(|_| owner.reserve().unwrap()).collect();
            for &handle in &handles { owner.publish(handle).unwrap(); }
            let mut waiting = [None; CAPACITY];
            for index in 0..CAPACITY - 1 {
                seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
                let event = seed as usize % EVENT_CAPACITY;
                let handle = owner.select().unwrap(); assert_eq!(handle, handles[index]);
                assert_eq!(owner.observe(handle, event as u32).unwrap().epoch, epochs[event]);
                assert_eq!(owner.wait(handle, event as u32, epochs[event]).unwrap().outcome, 1);
                waiting[index] = Some(event); commands += 3;
            }
            let producer = owner.select().unwrap();
            let mut order = VecDeque::new();
            // Independent oracle uses optional event IDs and a dynamic deque.
            for offset in 0..EVENT_CAPACITY {
                let event = (offset + seed as usize % EVENT_CAPACITY) % EVENT_CAPACITY;
                epochs[event] += 1;
                let mut count = 0;
                for (index, slot) in waiting.iter_mut().enumerate() {
                    if *slot == Some(event) { *slot = None; order.push_back(index); count += 1; }
                }
                let result = owner.signal(producer, event as u32).unwrap();
                assert_eq!((result.epoch, result.woken), (epochs[event], count));
                for (index, handle) in handles.iter().enumerate().take(CAPACITY - 1) {
                    let expected = if waiting[index].is_some() { State::Waiting } else { State::Ready };
                    assert_eq!(owner.state(*handle), Ok(expected));
                }
                assert!(owner.validate()); commands += 1;
            }
            owner.yield_task(producer).unwrap(); order.push_back(CAPACITY - 1);
            while let Some(index) = order.pop_front() {
                assert_eq!(owner.select(), Ok(handles[index]));
                owner.exit(handles[index]).unwrap(); owner.collect(handles[index]).unwrap(); commands += 3;
            }
            assert_eq!(owner.select(), Err(Error::Empty));
        }
    }
    println!("Event oracle: 32 traces, 4096 lifecycles, {commands} commands compared");
}

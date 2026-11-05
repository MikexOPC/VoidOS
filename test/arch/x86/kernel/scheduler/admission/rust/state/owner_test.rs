use super::*;
use std::{sync::{Arc,Barrier},thread};

// ====== lifecycle_and_authority ======

#[test]
fn lifecycle_and_authority() {
    let owner = Admission::new();
    assert_eq!(owner.claim(1,1,0x1000),0);
    assert_eq!(owner.configure(4,0x1000,1),0);
    for count in [0,1,65,u32::MAX] { assert_eq!(owner.configure(count,0x1000,0),0); }
    for group in [0,INVALID] { assert_eq!(owner.configure(4,group,0),0); }
    assert_eq!(owner.configure(4,0x1000,0),1);
    assert_eq!(owner.configure(4,0x1000,0),0);
    for cpu in [0,4,64,u32::MAX] { assert_eq!(owner.offer(cpu,0,7,0x1000),0); }
    for token in [0,MAX_TOKEN + 1,INVALID] { assert_eq!(owner.offer(1,0,token,0x1000),0); }
    assert_eq!(owner.offer(1,2,7,0x1000),0);
    assert_eq!(owner.offer(1,0,7,0x2000),0);
    assert_eq!(owner.offer(1,0,7,0x1000),1);
    assert_eq!(owner.offer(1,0,8,0x1000),0);
    assert_eq!(owner.claim(1,2,0x1000),INVALID);
    assert_eq!(owner.claim(1,1,0x2000),0);
    assert_eq!(owner.admit(1,1,7,0x1000),0);
    assert_eq!(owner.claim(1,1,0x1000),7);
    assert_eq!(owner.claim(1,1,0x1000),0);
    assert_eq!(owner.admit(1,0,7,0x1000),0);
    assert_eq!(owner.admit(1,1,6,0x1000),0);
    assert_eq!(owner.fail(1,2,7,0x1000),0);
    assert_eq!(owner.admit(1,1,7,0x1000),1);
    assert_eq!(owner.value(1,0x1000),(7 << 3) | ADMITTED);
    assert_eq!(owner.value(2,0x1000),0);
    assert_eq!(owner.value(1,0x2000),INVALID);
}

// ====== claim_is_exclusive ======

#[test]
fn claim_is_exclusive() {
    let owner = Arc::new(Admission::new());
    assert_eq!(owner.configure(64,5,0),1);
    assert_eq!(owner.offer(63,0,MAX_TOKEN,5),1);
    let barrier = Arc::new(Barrier::new(8));
    let workers: Vec<_> = (0..8).map(|_| {
        let owner = Arc::clone(&owner); let barrier = Arc::clone(&barrier);
        thread::spawn(move || { barrier.wait(); owner.claim(63,63,5) })
    }).collect();
    let values: Vec<_> = workers.into_iter().map(|worker| worker.join().unwrap()).collect();
    assert_eq!(values.iter().filter(|&&token| token == MAX_TOKEN).count(),1);
    assert_eq!(values.iter().filter(|&&token| token == 0).count(),7);
    assert_eq!(owner.admit(63,63,MAX_TOKEN,5),1);
}

// ====== failure_wins_completion_race ======

#[test]
fn failure_wins_completion_race() {
    for _ in 0..512 {
        let owner = Arc::new(Admission::new());
        assert_eq!(owner.configure(4,9,0),1);
        assert_eq!(owner.offer(1,0,11,9),1);
        assert_eq!(owner.claim(1,1,9),11);
        let other = Arc::clone(&owner);
        let worker = thread::spawn(move || other.admit(1,1,11,9));
        assert_eq!(owner.fail(1,0,11,9),1);
        assert!(worker.join().unwrap() <= 1);
        assert_eq!(owner.value(1,9),(11 << 3) | FAILED);
        assert_eq!(owner.admit(1,1,11,9),0);
        assert_eq!(owner.claim(1,1,9),0);
        assert_eq!(owner.offer(1,0,12,9),0);
    }
}

// ====== timeout_before_or_after_claim ======

#[test]
fn timeout_before_or_after_claim() {
    for claimed in [false,true] {
        let owner = Admission::new();
        assert_eq!(owner.configure(2,9,0),1);
        assert_eq!(owner.offer(1,0,17,9),1);
        if claimed { assert_eq!(owner.claim(1,1,9),17); }
        assert_eq!(owner.fail(1,0,17,9),1);
        assert_eq!(owner.claim(1,1,9),0);
        assert_eq!(owner.admit(1,1,17,9),0);
        assert_eq!(owner.value(1,9),(17 << 3) | FAILED);
    }
}

// ====== terminal_tokens_and_each_cpu ======

#[test]
fn terminal_tokens_and_each_cpu() {
    let owner = Admission::new();
    assert_eq!(owner.configure(64,9,0),1);
    for cpu in 1..64 {
        let token = u64::from(cpu);
        assert_eq!(owner.offer(cpu,0,token,9),1);
        assert_eq!(owner.fail(cpu,0,token + 1,9),0);
        assert_eq!(owner.claim(cpu,cpu,9),token);
        assert_eq!(owner.admit(cpu,cpu,token,9),1);
        assert_eq!(owner.fail(cpu,cpu,token,9),1);
        assert_eq!(owner.fail(cpu,0,token,9),1);
        assert_eq!(owner.value(cpu,9),(token << 3) | FAILED);
    }
}

// ====== execution_and_quiescence ======

#[test]
fn execution_and_quiescence() {
    let owner = Admission::new();
    assert_eq!(owner.configure(4,9,0),1);
    assert_eq!(owner.offer(1,0,MAX_TOKEN,9),1);
    assert_eq!(owner.claim(1,1,9),MAX_TOKEN);
    assert_eq!(owner.transition(1,0,MAX_TOKEN,9,0),0);
    assert_eq!(owner.admit(1,1,MAX_TOKEN,9),1);
    assert_eq!(owner.transition(1,1,MAX_TOKEN,9,0),0);
    assert_eq!(owner.transition(1,0,MAX_TOKEN,9,0),1);
    assert_eq!(owner.collect_allowed(1,0,MAX_TOKEN,9),0);
    assert_eq!(owner.transition(1,0,MAX_TOKEN,9,1),0);
    assert_eq!(owner.transition(1,1,MAX_TOKEN,9,1),1);
    assert_eq!(owner.collect_allowed(1,0,MAX_TOKEN,9),0);
    assert_eq!(owner.release(1,1,MAX_TOKEN,9,1),0);
    assert_eq!(owner.release(1,1,MAX_TOKEN,9,0),0);
    for cpu in [0,4,64,u32::MAX] { assert_eq!(owner.release(cpu,0,MAX_TOKEN,9,1),0); }
    assert_eq!(owner.release(1,0,MAX_TOKEN,9,1),1);
    assert_eq!(owner.release(1,1,MAX_TOKEN,9,0),1);
    assert_eq!(owner.transition(1,0,MAX_TOKEN,9,2),0);
    assert_eq!(owner.transition(1,1,MAX_TOKEN,9,2),1);
    assert_eq!(owner.value(1,9),MAX_TOKEN << 3 | QUIESCED);
    assert_ne!(owner.value(1,9),INVALID);
    assert_eq!(owner.collect_allowed(1,0,MAX_TOKEN,9),1);
    assert_eq!(owner.collect_allowed(1,1,MAX_TOKEN,9),0);
    assert_eq!(owner.collect_allowed(1,0,MAX_TOKEN - 1,9),0);
    assert_eq!(owner.release(1,0,MAX_TOKEN,9,1),0);
    assert_eq!(owner.transition(1,0,MAX_TOKEN,9,0),0);
    assert_eq!(owner.fail(1,0,MAX_TOKEN,9),1);
    assert_eq!(owner.collect_allowed(1,0,MAX_TOKEN,9),0);
}

// ====== release_and_quiescence_race ======

#[test]
fn release_and_quiescence_race() {
    for _ in 0..128 {
        let owner = Arc::new(Admission::new());
        assert_eq!(owner.configure(2,9,0),1);
        assert_eq!(owner.offer(1,0,1,9),1);
        assert_eq!(owner.claim(1,1,9),1);
        assert_eq!(owner.admit(1,1,1,9),1);
        assert_eq!(owner.transition(1,0,1,9,0),1);
        assert_eq!(owner.transition(1,1,1,9,1),1);
        let other = Arc::clone(&owner);
        let worker = thread::spawn(move || {
            for _ in 0..100000 {
                if other.release(1,1,1,9,0) == 1 { return other.transition(1,1,1,9,2); }
                thread::yield_now();
            }
            0
        });
        assert_eq!(owner.release(1,0,1,9,1),1);
        assert_eq!(worker.join().unwrap(),1);
        assert_eq!(owner.collect_allowed(1,0,1,9),1);
    }
}

// ====== execution_failure_races ======

#[test]
fn execution_failure_races() {
    for operation in 0..3 {
        for _ in 0..128 {
            let owner = Arc::new(Admission::new());
            assert_eq!(owner.configure(2,9,0),1);
            assert_eq!(owner.offer(1,0,1,9),1);
            assert_eq!(owner.claim(1,1,9),1);
            assert_eq!(owner.admit(1,1,1,9),1);
            for before in 0..operation {
                assert_eq!(owner.transition(1,if before == 0 {0} else {1},1,9,before),1);
            }
            let other = Arc::clone(&owner);
            let worker = thread::spawn(move || other.transition(1,if operation == 0 {0} else {1},1,9,operation));
            assert_eq!(owner.fail(1,0,1,9),1);
            assert!(worker.join().unwrap() <= 1);
            assert_eq!(owner.value(1,9),1 << 3 | FAILED);
        }
    }
}

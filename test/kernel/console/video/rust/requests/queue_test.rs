#[path = "../../../../../../kernel/console/video/rust/requests/queue.rs"]
mod queue;
use queue::{CAPACITY, Error, Operation, Queue, State, TEXT_LIMIT};

// ====== fifo_and_wrap ======

#[test]
fn fifo_and_wrap() {
    let mut owner = Queue::default();
    for round in 0..1000 {
        for index in 0..CAPACITY {
            owner.push(Operation::Write, &[(usize::from(b'a') + (round + index) % 26) as u8]).unwrap();
        }
        assert_eq!(owner.push(Operation::Submit, b""), Err(Error::Full));
        for index in 0..CAPACITY {
            let request = owner.take().unwrap();
            assert_eq!(request.operation(), Operation::Write);
            assert_eq!(request.text(), &[(usize::from(b'a') + (round + index) % 26) as u8]);
            owner.complete(true).unwrap();
        }
        assert_eq!(owner.pending(), 0);
        assert_eq!(owner.take(), Err(Error::Empty));
    }
}

// ====== validation_before_publication ======

#[test]
fn validation_before_publication() {
    let mut owner = Queue::new();
    for text in [&[0xff][..], &[0xc0, 0x80], &[0xed, 0xa0, 0x80], &[0xe2, 0x82],
        &[0xf4, 0x90, 0x80, 0x80], &[0], &[0x7f], &[0xc2, 0x85], &[b'a'; 257]] {
        assert_eq!(owner.push(Operation::Write, text), Err(Error::Invalid));
        assert_eq!(owner.pending(), 0);
    }
    assert_eq!(owner.push(Operation::Edit, b"\n"), Err(Error::Invalid));
    assert_eq!(owner.push(Operation::Submit, b"x"), Err(Error::Invalid));
    for operation in [Operation::Write, Operation::Edit, Operation::Submit] {
        owner.push(operation, b"").unwrap();
        assert_eq!(owner.take().unwrap().operation(), operation);
        owner.complete(true).unwrap();
    }
    owner.push(Operation::Write, "città è già più\r\n".as_bytes()).unwrap();
    assert_eq!(owner.take().unwrap().text(), "città è già più\r\n".as_bytes());
    owner.complete(true).unwrap();
    assert_eq!(owner.pending(), 0);
}

// ====== owns_payload_and_capacity ======

#[test]
fn owns_payload_and_capacity() {
    let mut owner = Queue::new();
    let mut bytes = [b'a'; TEXT_LIMIT];
    owner.push(Operation::Edit, &bytes).unwrap();
    bytes.fill(b'z');
    let request = owner.take().unwrap();
    assert_eq!(request.text(), &[b'a'; TEXT_LIMIT]);
    assert_eq!(owner.take(), Err(Error::InFlight));
    for _ in 0..CAPACITY { owner.push(Operation::Submit, b"").unwrap(); }
    assert_eq!(owner.push(Operation::Submit, b""), Err(Error::Full));
    assert_eq!(request.text(), &[b'a'; TEXT_LIMIT]);
    owner.complete(true).unwrap();
}

// ====== close_requires_drain_and_completion ======

#[test]
fn close_requires_drain_and_completion() {
    let mut owner = Queue::new();
    assert_eq!(owner.close(), Err(Error::State));
    assert_eq!(owner.complete(true), Err(Error::InFlight));
    owner.push(Operation::Submit, b"").unwrap();
    owner.drain().unwrap();
    assert_eq!(owner.close(), Err(Error::Pending));
    assert_eq!(owner.push(Operation::Write, b"x"), Err(Error::State));
    assert_eq!(owner.drain(), Err(Error::State));
    owner.take().unwrap();
    assert_eq!(owner.close(), Err(Error::InFlight));
    owner.complete(true).unwrap();
    owner.close().unwrap();
    assert_eq!(owner.state(), State::Closed);
    assert_eq!(owner.take(), Err(Error::State));
    assert_eq!(owner.complete(true), Err(Error::State));
    assert_eq!(owner.close(), Err(Error::State));
}

// ====== failure_is_terminal ======

#[test]
fn failure_is_terminal() {
    for draining in [false, true] {
        let mut owner = Queue::new();
        owner.push(Operation::Submit, b"").unwrap();
        owner.push(Operation::Write, b"retained").unwrap();
        owner.take().unwrap();
        if draining { owner.drain().unwrap(); }
        owner.complete(false).unwrap();
        assert_eq!(owner.state(), State::Failed);
        assert_eq!(owner.pending(), 1);
        assert_eq!(owner.push(Operation::Submit, b""), Err(Error::State));
        assert_eq!(owner.take(), Err(Error::State));
        assert_eq!(owner.complete(true), Err(Error::State));
        assert_eq!(owner.close(), Err(Error::State));
        assert_eq!(owner.drain(), Err(Error::State));
        assert_eq!(owner.pending(), 1);
    }
}

// ====== mixed_schedule_matches_reference ======

#[test]
fn mixed_schedule_matches_reference() {
    use std::collections::VecDeque;
    let mut owner = Queue::new();
    let mut reference = VecDeque::new();
    let mut flight = None;
    let mut seed = 0x7619_u32;
    for _ in 0..100_000 {
        seed = seed.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        match seed % 3 {
            0 => {
                let text = [b'a' + ((seed >> 8) % 26) as u8];
                let result = owner.push(Operation::Write, &text);
                if reference.len() == CAPACITY { assert_eq!(result, Err(Error::Full)); }
                else { result.unwrap(); reference.push_back(text); }
            }
            1 => {
                let result = owner.take();
                if flight.is_some() { assert_eq!(result, Err(Error::InFlight)); }
                else if let Some(expected) = reference.pop_front() {
                    let request = result.unwrap();
                    assert_eq!(request.text(), &expected);
                    flight = Some(request);
                } else { assert_eq!(result, Err(Error::Empty)); }
            }
            _ => {
                let result = owner.complete(true);
                if flight.take().is_some() { result.unwrap(); }
                else { assert_eq!(result, Err(Error::InFlight)); }
            }
        }
        assert_eq!(owner.pending(), reference.len());
        assert_eq!(owner.state(), State::Open);
    }
    owner.drain().unwrap();
    if flight.is_some() { owner.complete(true).unwrap(); }
    for expected in reference {
        assert_eq!(owner.take().unwrap().text(), &expected);
        owner.complete(true).unwrap();
    }
    owner.close().unwrap();
}

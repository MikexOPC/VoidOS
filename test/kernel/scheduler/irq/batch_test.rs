use super::*;

// ====== deferred_batch_is_transactional ======

#[test]
fn deferred_batch_is_transactional() {
    let mut owner = Owner::new();
    let a = owner.reserve().unwrap(); let b = owner.reserve().unwrap();
    owner.publish(a).unwrap(); owner.publish(b).unwrap();
    assert_eq!(owner.select(), Ok(a));
    let before = owner.copy_state();
    assert_eq!(owner.signal_pending(1), Err(Error::Running)); assert_eq!(owner, before);
    owner.wait(a, 0, 0).unwrap(); assert_eq!(owner.select(), Ok(b)); owner.wait(b, 1, 0).unwrap();
    let before = owner.copy_state();
    for mask in [0, 256, u32::MAX] {
        assert_eq!(owner.signal_pending(mask), Err(Error::InvalidEvent)); assert_eq!(owner, before);
    }
    owner.epochs[7] = u64::MAX;
    let before = owner.copy_state();
    assert_eq!(owner.signal_pending(129), Err(Error::CounterExhausted)); assert_eq!(owner, before);
    assert_eq!(owner.signal_pending(3).unwrap().woken, 2);
    assert_eq!(owner.signal_pending(3).unwrap().woken, 0);
    assert_eq!(owner.select(), Ok(a)); owner.exit(a).unwrap(); owner.collect(a).unwrap();
    assert_eq!(owner.select(), Ok(b)); owner.exit(b).unwrap(); owner.collect(b).unwrap();
    assert_eq!(owner.select(), Err(Error::Empty)); assert!(owner.validate());
}

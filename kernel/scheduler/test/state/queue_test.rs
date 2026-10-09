use super::*;

// ====== queue_wrap_duplicates_and_capacity ======

#[test]
fn queue_wrap_duplicates_and_capacity() {
    let mut queue = Queue::new();
    assert_eq!(queue.pop(), None);
    assert!(!queue.push(NONE));
    assert!(!queue.push(8));
    for index in 0..8 { assert!(queue.push(index)); }
    let full = queue;
    assert!(!queue.push(0));
    assert_eq!(queue, full);
    for round in 0..20_000 {
        let index = (round % 8) as u32;
        assert_eq!(queue.pop(), Some(index));
        assert!(queue.push(index));
        assert!(queue.validate());
    }
    for index in 0..8 { assert_eq!(queue.pop(), Some(index)); }
    assert_eq!(queue.pop(), None);
}

// ====== queue_corruption_is_rejected ======

#[test]
fn queue_corruption_is_rejected() {
    for variant in 0..5 {
        let mut queue = Queue::new();
        assert!(queue.push(0));
        assert!(queue.push(1));
        match variant {
            0 => queue.head = 8,
            1 => queue.count = 9,
            2 => queue.entries[1] = 0,
            3 => queue.entries[0] = NONE,
            _ => queue.entries[7] = 4,
        }
        let before = queue;
        assert!(!queue.validate());
        assert!(!queue.push(2));
        assert_eq!(queue.pop(), None);
        assert_eq!(queue, before);
    }
}

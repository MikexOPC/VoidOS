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

// ====== ring_matches_queue_model ======

#[test]
fn ring_matches_queue_model() {
    let mut storage = vec![NONE; CAPACITY];
    let mut ring = Ring { entries: storage.as_mut_ptr(), cap: CAPACITY, head: 0, count: 0 };
    let mut queue = Queue::new();
    let mut seed = 0x9E37_79B9_7F4A_7C15u64;
    for _ in 0..50_000 {
        seed = seed.wrapping_mul(6_364_136_223_846_793_005).wrapping_add(1_442_695_040_888_963_407);
        let value = (seed >> 33) as u32 % 10;
        match (seed >> 60) % 3 {
            0 => assert_eq!(ring.push(value), queue.push(value)),
            1 => assert_eq!(ring.pop(), queue.pop()),
            _ => {
                let keep = |index: u32| index % 2 == value % 2;
                assert_eq!(ring.take_first(keep), queue.take_first(keep));
            }
        }
        assert_eq!(ring.len(), queue.len());
        assert_eq!(ring.validate(), queue.validate());
        assert_eq!((ring.head, ring.count), (queue.head, queue.count));
        assert_eq!(ring.view(), &queue.entries[..]);
    }
}

// ====== ring_large_capacity_wraps ======

#[test]
fn ring_large_capacity_wraps() {
    for cap in [9usize, 64, 256] {
        let mut storage = vec![NONE; cap];
        let mut ring = Ring { entries: storage.as_mut_ptr(), cap, head: 0, count: 0 };
        assert!(!ring.push(NONE));
        assert!(!ring.push(cap as u32));
        for index in 0..cap as u32 { assert!(ring.push(index)); }
        assert!(!ring.push(0));
        for round in 0..cap * 40 {
            let index = (round % cap) as u32;
            assert_eq!(ring.pop(), Some(index));
            assert!(ring.push(index));
            assert!(ring.validate());
        }
        for index in 0..cap as u32 { assert_eq!(ring.pop(), Some(index)); }
        assert_eq!(ring.pop(), None);
    }
}

// ====== ring_corruption_is_rejected ======

#[test]
fn ring_corruption_is_rejected() {
    for variant in 0..4 {
        let mut storage = vec![NONE; 64];
        let mut ring = Ring { entries: storage.as_mut_ptr(), cap: 64, head: 0, count: 0 };
        assert!(ring.push(0));
        assert!(ring.push(1));
        match variant {
            0 => ring.head = 64,
            1 => ring.count = 65,
            2 => ring.view_mut()[1] = 0,
            _ => ring.view_mut()[63] = 5,
        }
        assert!(!ring.validate());
        assert!(!ring.push(2));
        assert_eq!(ring.pop(), None);
    }
}

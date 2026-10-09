use super::*;
use std::collections::VecDeque;

// ====== full_capacity_and_ordered_loss ======

#[test]
fn full_capacity_and_ordered_loss() {
    let mut bytes = Bytes::new();
    assert_eq!(bytes.take(), EMPTY);
    for byte in 0..=255 { assert!(bytes.push(byte)); }
    assert!(!bytes.push(9));
    assert_eq!(bytes.take(), DATA);
    assert!(!bytes.push(10)); // Space exists, but the loss barrier still closes intake.
    for byte in 1..=255 { assert_eq!(bytes.take(), DATA | byte); }
    assert!(!bytes.idle());
    assert_eq!(bytes.take(), LOSS | (2 << 32));
    assert!(bytes.idle());
    assert!(bytes.push(11)); assert_eq!(bytes.take(), DATA | 11);
    assert_eq!(bytes.take(), EMPTY);
    assert!(bytes.data.iter().all(|byte| *byte == 0));
}

// ====== bounded_model_stress ======

#[test]
fn bounded_model_stress() {
    let mut bytes = Bytes::new();
    let mut model = VecDeque::new();
    let (mut loss, mut lost, mut random) = (false, 0u32, 19u32);
    for _ in 0..500000 {
        random = random.wrapping_mul(1664525).wrapping_add(1013904223);
        if random & 3 != 0 {
            let byte = (random >> 24) as u8;
            let accepted = model.len() < 256 && !loss;
            assert_eq!(bytes.push(byte), accepted);
            if accepted { model.push_back(byte); } else { loss = true; lost += 1; }
        } else {
            let expected = if let Some(byte) = model.pop_front() { DATA | u64::from(byte) }
                else if loss { loss = false; LOSS | (u64::from(lost) << 32) } else { EMPTY };
            assert_eq!(bytes.take(), expected);
        }
        assert_eq!(bytes.idle(), model.is_empty() && !loss);
    }
}

// ====== loss_counter_saturates ======

#[test]
fn loss_counter_saturates() {
    let mut bytes = Bytes::new();
    bytes.loss = true; bytes.lost = u32::MAX;
    assert!(!bytes.push(7));
    assert_eq!(bytes.take(), LOSS | (u64::from(u32::MAX) << 32));
    assert!(bytes.push(8)); assert_eq!(bytes.take(), DATA | 8);
}

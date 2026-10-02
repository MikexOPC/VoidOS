#[path = "../../../../../mm/objects/rust/validation/bounds.rs"]
mod bounds;
use bounds::*;

// ====== dimensioni allineamenti e overflow rifiutati ===== //

#[test]
fn invalid_requests() {
    for bytes in [0, 65537, u64::MAX] { assert_eq!(kernel_object_request(bytes, 16), 0); }
    for alignment in [0, 3, 31, 8192, u64::MAX] { assert_eq!(kernel_object_request(1, alignment), 0); }
    assert_eq!(kernel_object_position(u64::MAX, u64::MAX, 1, 1), 0);
    assert_eq!(kernel_object_position(0, u64::MAX, 1, 16), 0);
}

// ====== estremi esatti del gap e padding di allineamento ===== //

#[test]
fn gaps_and_records() {
    for shift in 0..=12 {
        let alignment = 1 << shift;
        assert_eq!(kernel_object_position(1, alignment + 17, 17, alignment), alignment + 1);
        assert_eq!(kernel_object_position(1, alignment + 16, 17, alignment), 0);
        assert_eq!(kernel_object_record(alignment, 17, alignment, 65536), 1);
    }
    assert_eq!(kernel_object_position(0, 65536, 65536, 4096), 1);
    assert_eq!(kernel_object_record(65535, 1, 1, 65536), 1);
    assert_eq!(kernel_object_record(65535, 2, 1, 65536), 0);
    assert_eq!(kernel_object_record(1, 1, 2, 65536), 0);
    assert_eq!(kernel_object_record(0, 1, 1, 0), 0);
}

// ====== modello u128 indipendente per 8192 gap ===== //

#[test]
fn independent_alignment_model() {
    let mut state = 0x1357_2468_abcdu64;
    for _ in 0..8192 {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        let start = state % 65537;
        let end = start + (state >> 16) % (65537 - start);
        let bytes = 1 + (state >> 32) % 4096;
        let alignment = 1u64 << ((state >> 48) % 13);
        let aligned = (u128::from(start) + u128::from(alignment) - 1) / u128::from(alignment) * u128::from(alignment);
        let expected = if aligned + u128::from(bytes) <= u128::from(end) { aligned as u64 + 1 } else { 0 };
        assert_eq!(kernel_object_position(start, end, bytes, alignment), expected);
    }
}

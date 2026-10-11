#[path = "../../../../../lib/memory/rust/capacity/buffer_capacity.rs"]
mod buffer_capacity;
use buffer_capacity::{os_buffer_capacity as capacity, os_buffer_extent_is_valid as extent};

// ====== oracolo a 128 bit separato dall'aritmetica di produzione ====== //

fn reference(needed: u64, entry: u64, extra: u64, alignment: u64, maximum: u64) -> u64 {
    if needed == 0 || entry == 0 || alignment == 0 || alignment.count_ones() != 1 {
        return 0;
    }
    let total = u128::from(needed) + u128::from(entry) * u128::from(extra);
    let align = u128::from(alignment);
    let rounded = total.div_ceil(align) * align;
    if rounded > u128::from(maximum) { 0 } else { rounded as u64 }
}

#[test]
fn pool_and_page_profiles_keep_the_same_limits() {
    assert_eq!(capacity(96, 48, 8, 1, 1 << 20), 480);
    assert_eq!(capacity(96, 48, 8, 4096, 1 << 20), 4096);
    assert_eq!(capacity(4096, 48, 8, 4096, 1 << 20), 8192);
    assert_eq!(capacity((1 << 20) - 384, 48, 8, 4096, 1 << 20), 1 << 20);
    assert_eq!(capacity((1 << 20) - 383, 48, 8, 4096, 1 << 20), 0);
}

#[test]
fn all_overflow_stages_and_invalid_alignments_are_rejected() {
    assert_eq!(capacity(1, u64::MAX, 2, 1, u64::MAX), 0);
    assert_eq!(capacity(u64::MAX, 1, 1, 1, u64::MAX), 0);
    assert_eq!(capacity(u64::MAX, 1, 0, 4096, u64::MAX), 0);
    assert_eq!(capacity(u64::MAX, 1, 0, 1, u64::MAX), u64::MAX);
    for alignment in [0, 3, 4095, u64::MAX] {
        assert_eq!(capacity(1, 1, 0, alignment, u64::MAX), 0);
    }
    assert_eq!(capacity(0, 48, 8, 4096, u64::MAX), 0);
    assert_eq!(capacity(1, 0, 8, 4096, u64::MAX), 0);
    assert_eq!(capacity(1, 1, 0, 1, 0), 0);
}

#[test]
fn boundary_grid_matches_wider_integer_oracle() {
    let values = [0, 1, 39, 40, 48, 4095, 4096, (1 << 20) - 384, 1 << 20, 1 << 63, u64::MAX - 1, u64::MAX];
    for needed in values {
        for entry in values {
            for extra in [0, 1, 8, u64::MAX] {
                for alignment in [0, 1, 3, 4096, 1 << 63] {
                    for maximum in [0, 4096, 1 << 20, u64::MAX] {
                        assert_eq!(capacity(needed, entry, extra, alignment, maximum), reference(needed, entry, extra, alignment, maximum));
                    }
                }
            }
        }
    }
}

#[test]
fn generated_sizes_preserve_rounding_and_budget() {
    let mut seed = 0x1234_5678_9abc_def0u64;
    for _ in 0..10_000 {
        seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
        let needed = seed;
        let entry = (seed >> 32) & 8191;
        let extra = (seed >> 48) & 255;
        let alignment = 1u64 << ((seed >> 16) & 63);
        assert_eq!(capacity(needed, entry, extra, alignment, u64::MAX), reference(needed, entry, extra, alignment, u64::MAX));
    }
}

#[test]
fn numeric_extent_checks_do_not_claim_pointer_accessibility() {
    for base in [0, 1, 4096, u64::MAX - 4096, u64::MAX] {
        for bytes in [0, 1, 4096, u64::MAX] {
            for maximum in [0, 4096, u64::MAX] {
                let expected = base != 0 && bytes != 0 && bytes <= maximum &&
                    u128::from(base) + u128::from(bytes) <= u128::from(u64::MAX);
                assert_eq!(extent(base, bytes, maximum), u32::from(expected));
            }
        }
    }
}

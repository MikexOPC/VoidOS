#[path = "../../../../../lib/security/bounds/rust/bounds.rs"]
mod bounds;

use bounds::*;

// ====== oracolo a 128 bit ======

const LIMIT: u128 = u64::MAX as u128;

const EDGES: [u64; 14] = [
    0, 1, 2, 3, 7, 8, 4095, 4096, 4097,
    1 << 32, 1 << 63, (1 << 63) + 1, u64::MAX - 1, u64::MAX,
];

fn value(result: OsBoundsValue) -> Option<u64> {
    assert!(result.valid <= 1);
    if result.valid == 1 {
        Some(result.value)
    } else {
        assert_eq!(result.value, 0);
        None
    }
}

fn wide(value: u64) -> u128 {
    u128::from(value)
}

fn fits(value: u128) -> Option<u64> {
    if value <= LIMIT { Some(value as u64) } else { None }
}

fn power_of_two(value: u64) -> bool {
    value != 0 && value.count_ones() == 1
}

fn end(base: u64, length: u64) -> Option<u128> {
    let total = wide(base) + wide(length);
    if total <= LIMIT { Some(total) } else { None }
}

struct Generator(u64);

impl Generator {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let mixed = self.0 ^ (self.0 >> 29);
        match mixed & 7 {
            0 => EDGES[(mixed >> 8) as usize % EDGES.len()],
            1 => mixed >> 40,
            2 => u64::MAX - (mixed >> 40),
            _ => mixed,
        }
    }
}

// ====== aritmetica ======

#[test]
fn arithmetic_matches_oracle_on_edges() {
    for left in EDGES {
        for right in EDGES {
            assert_eq!(value(os_bounds_add(left, right)), fits(wide(left) + wide(right)));
            assert_eq!(value(os_bounds_mul(left, right)), fits(wide(left) * wide(right)));
        }
    }
}

#[test]
fn arithmetic_rejects_overflow_at_the_limit() {
    assert_eq!(value(os_bounds_add(u64::MAX, 0)), Some(u64::MAX));
    assert_eq!(value(os_bounds_add(u64::MAX, 1)), None);
    assert_eq!(value(os_bounds_add(u64::MAX, u64::MAX)), None);
    assert_eq!(value(os_bounds_mul(1 << 32, (1 << 32) - 1)), Some(u64::MAX - ((1 << 32) - 1)));
    assert_eq!(value(os_bounds_mul(1 << 32, 1 << 32)), None);
    assert_eq!(value(os_bounds_mul(u64::MAX, 0)), Some(0));
}

// ====== allineamento ======

#[test]
fn alignment_matches_oracle_on_edges() {
    for number in EDGES {
        for alignment in EDGES {
            let good = power_of_two(alignment);
            let up = if good { fits(wide(number).div_ceil(wide(alignment)) * wide(alignment)) } else { None };
            let down = if good { Some(number / alignment * alignment) } else { None };
            assert_eq!(value(os_bounds_align_up(number, alignment)), up);
            assert_eq!(value(os_bounds_align_down(number, alignment)), down);
            assert_eq!(os_bounds_is_aligned(number, alignment), u32::from(good && number % alignment == 0));
        }
    }
}

#[test]
fn alignment_rejects_bad_alignments_and_overflow() {
    for alignment in [0, 3, 6, 4095, 4097, u64::MAX] {
        assert_eq!(value(os_bounds_align_up(4096, alignment)), None);
        assert_eq!(value(os_bounds_align_down(4096, alignment)), None);
        assert_eq!(os_bounds_is_aligned(0, alignment), 0);
    }
    assert_eq!(value(os_bounds_align_up(u64::MAX - 4094, 4096)), None);
    assert_eq!(value(os_bounds_align_up(u64::MAX - 4095, 4096)), Some(u64::MAX - 4095));
    assert_eq!(value(os_bounds_align_up((1 << 63) + 1, 1 << 63)), None);
    assert_eq!(value(os_bounds_align_up(0, 1 << 63)), Some(0));
}

// ====== range e contenitori ======

#[test]
fn range_end_is_exclusive_and_representable() {
    assert_eq!(value(os_bounds_range_end(4096, 4096)), Some(8192));
    assert_eq!(value(os_bounds_range_end(u64::MAX, 0)), Some(u64::MAX));
    assert_eq!(value(os_bounds_range_end(u64::MAX - 1, 1)), Some(u64::MAX));
    assert_eq!(value(os_bounds_range_end(u64::MAX, 1)), None);
    assert_eq!(value(os_bounds_range_end(1, u64::MAX)), None);
}

#[test]
fn containment_follows_policy_for_empty_and_edge_ranges() {
    assert_eq!(os_bounds_range_within(0x1000, 0x1000, 0x1000, 0x1000), 1);
    assert_eq!(os_bounds_range_within(0x1000, 0x1001, 0x1000, 0x1000), 0);
    assert_eq!(os_bounds_range_within(0x0fff, 1, 0x1000, 0x1000), 0);
    assert_eq!(os_bounds_range_within(0x2000, 0, 0x1000, 0x1000), 1);
    assert_eq!(os_bounds_range_within(0x2001, 0, 0x1000, 0x1000), 0);
    assert_eq!(os_bounds_range_within(0x1000, 0, 0x1000, 0), 1);
    assert_eq!(os_bounds_range_within(0x1000, 1, 0x1000, 0), 0);
    assert_eq!(os_bounds_slice_within(16, 0, 16), 1);
    assert_eq!(os_bounds_slice_within(17, 0, 16), 0);
    assert_eq!(os_bounds_slice_within(8, 8, 16), 1);
    assert_eq!(os_bounds_slice_within(8, 9, 16), 0);
}

#[test]
fn containment_rejects_wrapping_ranges() {
    assert_eq!(os_bounds_range_within(u64::MAX, 1, 0, u64::MAX), 0);
    assert_eq!(os_bounds_range_within(u64::MAX - 1, 2, 0, u64::MAX), 0);
    assert_eq!(os_bounds_range_within(0, 1, u64::MAX, 2), 0);
    assert_eq!(os_bounds_range_within(1, 1, 1, u64::MAX), 0);
    assert_eq!(os_bounds_slice_within(u64::MAX, 2, u64::MAX), 0);
    assert_eq!(os_bounds_slice_within(2, u64::MAX - 1, u64::MAX), 0);
    assert_eq!(os_bounds_slice_within(1, u64::MAX - 1, u64::MAX), 1);
}

#[test]
fn containment_matches_oracle_on_edges() {
    for base in EDGES {
        for length in EDGES {
            for outer_base in EDGES {
                for outer_length in EDGES {
                    let expected = match (end(base, length), end(outer_base, outer_length)) {
                        (Some(stop), Some(outer_stop)) => base >= outer_base && stop <= outer_stop,
                        _ => false,
                    };
                    assert_eq!(os_bounds_range_within(base, length, outer_base, outer_length), u32::from(expected));
                }
            }
            for container in EDGES {
                let expected = end(base, length).is_some_and(|stop| stop <= wide(container));
                assert_eq!(os_bounds_slice_within(base, length, container), u32::from(expected));
            }
        }
    }
}

// ====== indici e array ======

#[test]
fn indices_and_elements_stay_inside_their_table() {
    assert_eq!(os_bounds_index_valid(0, 1), 1);
    assert_eq!(os_bounds_index_valid(1, 1), 0);
    assert_eq!(os_bounds_index_valid(0, 0), 0);
    assert_eq!(os_bounds_index_valid(u64::MAX, u64::MAX), 0);
    assert_eq!(value(os_bounds_element_offset(3, 4, 16)), Some(48));
    assert_eq!(value(os_bounds_element_offset(4, 4, 16)), None);
    assert_eq!(value(os_bounds_element_offset(0, 4, 0)), None);
    assert_eq!(value(os_bounds_element_offset(1, 2, u64::MAX)), None);
    assert_eq!(value(os_bounds_element_offset(0, 1, u64::MAX)), Some(0));
    assert_eq!(value(os_bounds_element_offset(1 << 32, u64::MAX, 1 << 32)), None);
    assert_eq!(value(os_bounds_array_bytes(4, 16, 64)), Some(64));
    assert_eq!(value(os_bounds_array_bytes(5, 16, 64)), None);
    assert_eq!(value(os_bounds_array_bytes(0, 16, 0)), Some(0));
    assert_eq!(value(os_bounds_array_bytes(4, 0, 64)), None);
    assert_eq!(value(os_bounds_array_bytes(1 << 32, 1 << 32, u64::MAX)), None);
}

#[test]
fn elements_match_oracle_on_edges() {
    for index in EDGES {
        for count in EDGES {
            for size in EDGES {
                let offset = wide(index) * wide(size);
                let expected = if size == 0 || index >= count || offset + wide(size) > LIMIT { None } else { Some(offset as u64) };
                assert_eq!(value(os_bounds_element_offset(index, count, size)), expected);
                let bytes = wide(count) * wide(size);
                let expected = if size == 0 || bytes > wide(index) { None } else { Some(bytes as u64) };
                assert_eq!(value(os_bounds_array_bytes(count, size, index)), expected);
            }
        }
    }
}

// ====== disgiunzione ======

#[test]
fn disjoint_ranges_are_half_open_and_fail_closed() {
    assert_eq!(os_bounds_ranges_disjoint(0x1000, 0x1000, 0x2000, 0x1000), 1);
    assert_eq!(os_bounds_ranges_disjoint(0x1000, 0x1001, 0x2000, 0x1000), 0);
    assert_eq!(os_bounds_ranges_disjoint(0x2000, 0x1000, 0x1000, 0x1001), 0);
    assert_eq!(os_bounds_ranges_disjoint(0x1000, 0x3000, 0x2000, 0x10), 0);
    assert_eq!(os_bounds_ranges_disjoint(0x1800, 0, 0x1000, 0x1000), 1);
    assert_eq!(os_bounds_ranges_disjoint(u64::MAX, 1, 0, 1), 0);
    assert_eq!(os_bounds_ranges_disjoint(0, 1, 1, u64::MAX), 0);
    assert_eq!(os_bounds_ranges_disjoint(u64::MAX, 2, u64::MAX, 0), 0);
}

#[test]
fn disjoint_ranges_match_oracle_on_edges() {
    for first_base in EDGES {
        for first_length in EDGES {
            for second_base in EDGES {
                for second_length in EDGES {
                    let expected = match (end(first_base, first_length), end(second_base, second_length)) {
                        (Some(first_end), Some(second_end)) => first_length == 0 || second_length == 0 ||
                            first_end <= wide(second_base) || second_end <= wide(first_base),
                        _ => false,
                    };
                    let forward = os_bounds_ranges_disjoint(first_base, first_length, second_base, second_length);
                    let backward = os_bounds_ranges_disjoint(second_base, second_length, first_base, first_length);
                    assert_eq!(forward, u32::from(expected));
                    assert_eq!(forward, backward);
                }
            }
        }
    }
}

// ====== input ostili generati ======

#[test]
fn generated_hostile_inputs_match_oracle() {
    let mut generator = Generator(0x5eed_0b00_0d5e_2026);
    for _ in 0..200_000 {
        let (a, b, c, d) = (generator.next(), generator.next(), generator.next(), generator.next());
        assert_eq!(value(os_bounds_add(a, b)), fits(wide(a) + wide(b)));
        assert_eq!(value(os_bounds_mul(a, b)), fits(wide(a) * wide(b)));
        let alignment = 1u64 << (c & 63);
        assert_eq!(value(os_bounds_align_up(a, alignment)), fits(wide(a).div_ceil(wide(alignment)) * wide(alignment)));
        let inside = match (end(a, b), end(c, d)) {
            (Some(stop), Some(outer_stop)) => a >= c && stop <= outer_stop,
            _ => false,
        };
        assert_eq!(os_bounds_range_within(a, b, c, d), u32::from(inside));
        let apart = match (end(a, b), end(c, d)) {
            (Some(first_end), Some(second_end)) => b == 0 || d == 0 || first_end <= wide(c) || second_end <= wide(a),
            _ => false,
        };
        assert_eq!(os_bounds_ranges_disjoint(a, b, c, d), u32::from(apart));
    }
}

// ====== layout ABI ======

#[test]
fn result_layout_is_stable_for_c_callers() {
    assert_eq!(core::mem::size_of::<OsBoundsValue>(), 16);
    assert_eq!(core::mem::align_of::<OsBoundsValue>(), 8);
    assert_eq!(core::mem::offset_of!(OsBoundsValue, value), 0);
    assert_eq!(core::mem::offset_of!(OsBoundsValue, valid), 8);
}

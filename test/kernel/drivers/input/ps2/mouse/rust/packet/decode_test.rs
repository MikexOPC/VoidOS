use super::*;

// ====== decode_packet ======

fn packet(header: u32, x: u32, y: u32) -> u64 {
    let a = step(0, header);
    assert_eq!(a as u32, PENDING as u32);
    let b = step((a >> 32) as u32, x);
    assert_eq!(b as u32, PENDING as u32);
    step((b >> 32) as u32, y)
}

// ====== all_deltas_and_buttons ======

#[test]
fn all_deltas_and_buttons() {
    for signs in 0..4_u32 {
        for buttons in 0..8_u32 {
            let header = 8 | (signs << 4) | buttons;
            for x in 0..256_u32 {
                for y in 0..256_u32 {
                    let result = packet(header, x, y);
                    assert_eq!(result >> 32, 0);
                    assert_eq!(result & 511, u64::from(x + ((signs & 1) << 8)));
                    assert_eq!((result >> 9) & 511,
                        u64::from(y + ((signs & 2) << 7)));
                    assert_eq!((result >> 18) & 7, u64::from(buttons));
                    assert_eq!(result >> 21, 0);
                }
            }
        }
    }
}

// ====== headers_overflow_and_no_early_event ======

#[test]
fn headers_overflow_and_no_early_event() {
    for header in 0..256_u32 {
        if header & 8 == 0 {
            assert_eq!(step(0, header), INVALID);
        } else if header & 0xc0 != 0 {
            assert_eq!(packet(header, 0xfa, 0xfe), OVERFLOW);
        }
    }
}

// ====== payload_is_not_command_reply ======

#[test]
fn payload_is_not_command_reply() {
    for byte in [0xfa, 0xfe, 0xaa, 0x08, 0xff] {
        let result = packet(8, byte, byte);
        assert_eq!(result & 511, u64::from(byte));
        assert_eq!((result >> 9) & 511, u64::from(byte));
    }
}

// ====== state_corruption_and_wide_bytes ======

#[test]
fn state_corruption_and_wide_bytes() {
    for state in [3, 4, 0x100, 0x101, 0x102, 0x10001,
        0x1000000, 0xffff_ffff] {
        assert_eq!(step(state, 8), INVALID);
    }
    for state in [0, 0x801, 0xff0802] {
        for byte in [256, 257, u32::MAX] {
            assert_eq!(step(state, byte), INVALID);
        }
    }
}

// ====== reset_at_each_partial_boundary ======

#[test]
fn reset_at_each_partial_boundary() {
    for count in 0..3 {
        let mut state = 0;
        for byte in [0x38, 0x80].iter().take(count) {
            state = (step(state, *byte) >> 32) as u32;
        }
        assert!(valid(state));
        state = 0;
        let mut result = 0;
        for byte in [8, 1, 2] {
            result = step(state, byte);
            state = (result >> 32) as u32;
        }
        assert_eq!(state, 0);
        assert_eq!(result, 1 | (2 << 9));
    }
}

// ====== valid_states_and_reserved_bits ======

#[test]
fn valid_states_and_reserved_bits() {
    for header in 0..256_u32 {
        if header & 8 == 0 {
            continue;
        }
        let first = 1 | (header << 8);
        assert!(valid(first));
        for bit in [2, 3, 4, 5, 6, 7, 16, 24, 31] {
            assert_eq!(step(first | (1 << bit), 0), INVALID);
        }
        for x in 0..256_u32 {
            let second = 2 | (header << 8) | (x << 16);
            assert!(valid(second));
            assert_eq!(step(second | (1 << 24), 0), INVALID);
            assert_eq!(step(second | 1, 0), INVALID);
        }
    }
}

// ====== arbitrary_input_preserves_transition_invariants ======

#[test]
fn arbitrary_input_preserves_transition_invariants() {
    let mut random = 0xabcddcba_u32;
    let mut state = 0;
    for index in 0..100_000 {
        random = random.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
        if index % 7 == 0 {
            state = random;
        }
        let result = step(state, random >> 24);
        state = (result >> 32) as u32;
        assert!(valid(state));
        if state != 0 {
            assert_eq!(result as u32, PENDING as u32);
        } else {
            assert_eq!(result & 0xf800_0000, 0);
        }
    }
}

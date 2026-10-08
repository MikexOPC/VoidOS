#[path = "../../../rust/validation/fields.rs"]
mod fields;
use fields::x86_hpet_description_fields as validate;
use fields::x86_hpet_table_span as span;
use fields::x86_hpet_capabilities_valid as capabilities;

// ====== timer_configuration_dependencies ======

#[test]
fn timer_configuration_dependencies() {
    use fields::x86_hpet_timer_valid as timer;
    for bit in [0, 7, 16, 20, 31] { assert_eq!(timer(0x2001, 1 << bit), 0); }
    for config in [8, 64, 256, 16384] { assert_eq!(timer(0x2001, config), 0); }
    assert_eq!(timer(1, 32), 0);
    for config in [0, 2, 4, 16, 24, 32, 288, 88, 32768, 49152, 31 << 9] {
        assert_eq!(timer(0x2001, config), 1);
    }
}

// ====== timer_candidate_boundaries_and_exclusions ======

#[test]
fn timer_candidate_boundaries_and_exclusions() {
    use fields::x86_hpet_timer_candidate as candidate;
    let id = 0xa201;
    for index in 0..3 {
        assert_eq!(candidate(id, 0, 0, index, 32, 1 << 31), 1);
        assert_eq!(candidate(id, 0, 0, index, 32768, 0), 1);
        assert_eq!(candidate(id, 0, 0, index, 32, 0), 0);
        assert_eq!(candidate(id, 0, 1 << index, index, 32, 1), 0);
        for config in [4, 24, 88, 49152, 1] {
            assert_eq!(candidate(id, 0, 0, index, config, 1), 0);
        }
        assert_eq!(candidate(id, 2, 0, index, 32, 1), u32::from(index == 2));
    }
    for index in [3, 31, 32, u32::MAX] {
        assert_eq!(candidate(id, 0, 0, index, 32, 1), 0);
    }
    assert_eq!(candidate(0x3f01, 0, 0, 31, 32, 1), 1);
    assert_eq!(candidate(0x3f01, 0, 1 << 31, 31, 32, 1), 0);
}

// ====== capability_identity_and_period ======

#[test]
fn capability_identity_and_period() {
    for period in [1, 10_000_000, 100_000_000] {
        assert_eq!(capabilities(0x8086a201, 0x8086a201, period), 1);
    }
    for period in [0, 100_000_001, u32::MAX] {
        assert_eq!(capabilities(0x8086a201, 0x8086a201, period), 0);
    }
    assert_eq!(capabilities(0x8086a201, 0x8086a202, 1), 0);
    assert_eq!(capabilities(0x8086a200, 0x8086a200, 1), 0);
    assert_eq!(capabilities(0x4001, 0x4001, 1), 0);
}

// ====== table_copy_boundaries ======

#[test]
fn table_copy_boundaries() {
    for bits in 32..=52 {
        let limit = 1u64 << bits;
        for bytes in 1..=56 {
            assert_eq!(span(limit - u64::from(bytes), bytes, bits), 1);
            assert_eq!(span(limit - u64::from(bytes) + 1, bytes, bits), 0);
            assert_eq!(span(4097, bytes, bits), 1);
        }
    }
    for bytes in [0, 57, u32::MAX] {
        assert_eq!(span(4096, bytes, 48), 0);
    }
    for bits in [0, 31, 53, 64, u32::MAX] {
        assert_eq!(span(4096, 4, bits), 0);
    }
    assert_eq!(span(4095, 4, 48), 0);
    assert_eq!(span(u64::MAX - 3, 4, 48), 0);
}

// ====== supported_profiles ======

#[test]
fn supported_profiles() {
    for bits in 32..=52 {
        for gas in [0, 64 << 8] {
            for attributes in [0, 1, 2, 0xf0, 0xf1, 0xf2] {
                assert_eq!(validate(0x8086a201, gas, 0xfed00000, bits, attributes), 1);
            }
        }
        assert_eq!(validate(1, 0, (1u64 << bits) - 1024, bits, 0), 1);
        assert_eq!(validate(1, 0, (1u64 << bits) - 1016, bits, 0), 0);
    }
}

// ====== malformed_fields ======

#[test]
fn malformed_fields() {
    for gas in [1, 8 << 8, 32 << 8, 1 << 16, 1 << 24] {
        assert_eq!(validate(1, gas, 4096, 48, 0), 0);
    }
    for bits in [0, 31, 53, 64, u32::MAX] {
        assert_eq!(validate(1, 0, 4096, bits, 0), 0);
    }
    for address in [0, 4095, 4097, u64::MAX, u64::MAX - 7] {
        assert_eq!(validate(1, 0, address, 48, 0), 0);
    }
    for id in [0, 0x8086a200, 0x4001] {
        assert_eq!(validate(id, 0, 4096, 48, 0), 0);
    }
    for attributes in [3, 15, 0xff, 256, u32::MAX] {
        assert_eq!(validate(1, 0, 4096, 48, attributes), 0);
    }
}

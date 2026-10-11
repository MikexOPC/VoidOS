#[path = "../../../../../../../../arch/x86/lib/processor/rust/paging/paging_capabilities.rs"]
mod paging_capabilities;
use paging_capabilities::x86_validate_paging_profile as cpu_validate_paging_profile;

// ====== confini di tutti i campi CPUID usati ====== //

#[test]
fn physical_and_linear_widths_are_bounded() {
    for physical in 0..=255 {
        for linear in 0..=255 {
            let expected = if (32..=52).contains(&physical) && matches!(linear, 48 | 57) {
                physical
            } else {
                0
            };
            assert_eq!(
                cpu_validate_paging_profile(
                    0x80000008,
                    (1 << 29) | (1 << 20),
                    physical | (linear << 8)
                ),
                expected
            );
        }
    }
}

#[test]
fn both_long_mode_and_nx_are_required() {
    for features in [
        0,
        1 << 29,
        1 << 20,
        u32::MAX & !(1 << 29),
        u32::MAX & !(1 << 20),
    ] {
        assert_eq!(
            cpu_validate_paging_profile(0x80000008, features, 48 << 8 | 52),
            0
        );
    }
    assert_eq!(
        cpu_validate_paging_profile(0x80000008, u32::MAX, 48 << 8 | 52),
        52
    );
}

#[test]
fn missing_leaf_rejects_even_plausible_register_contents() {
    for maximum in [0, 0x80000000, 0x80000001, 0x80000007] {
        assert_eq!(
            cpu_validate_paging_profile(maximum, u32::MAX, 48 << 8 | 52),
            0
        );
    }
}

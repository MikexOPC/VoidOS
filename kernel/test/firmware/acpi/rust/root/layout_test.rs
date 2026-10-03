#[path = "../../../../../firmware/acpi/rust/root/layout.rs"]
mod layout;
use layout::{kernel_acpi_root_layout as valid, kernel_acpi_physical_span as span};

// ====== lunghezze e numero di voci calcolati indipendentemente ===== //

#[test]
fn directory_lengths() {
    for (signature, width) in [(0x5444_5352, 4), (0x5444_5358, 8)] {
        for count in 0..=256 {
            assert_eq!(valid(signature, 36 + count * width, 1, width), count + 1);
        }
        assert_eq!(valid(signature, 36 + 257 * width, 1, width), 0);
        for length in 0..36 { assert_eq!(valid(signature, length, 1, width), 0); }
        for tail in 1..width { assert_eq!(valid(signature, 36 + tail, 1, width), 0); }
        assert_eq!(valid(signature ^ 1, 36, 1, width), 0);
        assert_eq!(valid(signature, 36, 2, width), 0);
    }
    for width in [0, 1, 2, 16, u32::MAX] { assert_eq!(valid(0x5444_5358, 36, 1, width), 0); }
}

// ====== intervalli fisici non allineati e overflow respinti ===== //

#[test]
fn physical_ranges() {
    for bits in 32..=52 {
        let limit = 1u64 << bits;
        assert_eq!(span(4097, 36, bits), 1);
        assert_eq!(span(limit - 36, 36, bits), 1);
        assert_eq!(span(limit - 35, 36, bits), 0);
        assert_eq!(span(limit, 36, bits), 0);
    }
    for base in [0, 4095, u64::MAX] { assert_eq!(span(base, 36, 52), 0); }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(span(4096, 36, bits), 0); }
    assert_eq!(span(4096, 0, 52), 0);
}

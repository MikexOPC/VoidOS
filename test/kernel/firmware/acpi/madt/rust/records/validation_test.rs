#[path = "../../../../../../../kernel/firmware/acpi/madt/rust/records/validation.rs"]
mod validation;
use validation::*;

// ====== header bounded e revisioni supportate ===== //

#[test]
fn header_bounds() {
    for revision in 1..=6 {
        assert_eq!(kernel_madt_header_fields(0x4349_5041, 44, revision), 1);
        assert_eq!(kernel_madt_header_fields(0x4349_5041, 65536, revision), 1);
    }
    for length in [0, 43, 65537, u32::MAX] { assert_eq!(kernel_madt_header_fields(0x4349_5041, length, 1), 0); }
    for revision in [0, 7, u32::MAX] { assert_eq!(kernel_madt_header_fields(0x4349_5041, 44, revision), 0); }
    for bit in 0..32 { assert_eq!(kernel_madt_header_fields(0x4349_5041 ^ (1 << bit), 44, 1), 0); }
}

// ====== dimensioni dei record senza leggere oltre il rimanente ===== //

#[test]
fn record_lengths() {
    for (kind, expected) in [(0,8),(1,12),(2,10),(3,8),(4,6),(5,12),(9,16),(10,12)] {
        for length in 0..=255 {
            assert_eq!(kernel_madt_record_span(kind, length, 255), u32::from(length == expected));
        }
        assert_eq!(kernel_madt_record_span(kind, expected, expected - 1), 0);
    }
    for kind in [6, 7, 8, 11, 127, 128, 255] {
        assert_eq!(kernel_madt_record_span(kind, 2, 2), 1);
        assert_eq!(kernel_madt_record_span(kind, 1, 2), 0);
        assert_eq!(kernel_madt_record_span(kind, 256, 512), 0);
    }
}

// ====== polarita trigger e maschere dei campi riservati ===== //

#[test]
fn interrupt_flags() {
    for flags in 0..=65535 {
        let expected = u32::from(flags & 3 != 2 && (flags >> 2) & 3 != 2);
        assert_eq!(kernel_madt_interrupt_flags(flags), expected);
    }
}

// ====== CPU legacy e x2APIC senza confondere online capable con enabled ===== //

#[test]
fn cpu_fields() {
    assert_eq!(kernel_madt_cpu_fields(255, 255, 1, 0), 1);
    assert_eq!(kernel_madt_cpu_fields(256, 1, 1, 0), 0);
    assert_eq!(kernel_madt_cpu_fields(1, 256, 1, 0), 0);
    assert_eq!(kernel_madt_cpu_fields(u32::MAX, u32::MAX, 2, 9), 1);
    for flags in [0, 3, 4, u32::MAX] { assert_eq!(kernel_madt_cpu_fields(0, 0, flags, 0), 0); }
    assert_eq!(kernel_madt_cpu_fields(0, 0, 1, 1), 0);
}

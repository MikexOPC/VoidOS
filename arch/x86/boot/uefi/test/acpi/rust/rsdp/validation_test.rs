#[path = "../../../../../../../../lib/firmware/acpi/rust/rsdp/validation.rs"]
mod validation;
use validation::os_acpi_rsdp_fields as valid;
const SIGNATURE: u64 = 0x2052_5450_2044_5352;

// ====== schemi legacy e moderno con revisioni compatibili ===== //

#[test]
fn supported_versions() {
    assert_eq!(valid(SIGNATURE, 0, 20, 0, 0, 4096, 0, 1, 32), 1);
    assert_eq!(valid(SIGNATURE, 0, 20, 0, 0, 4096, 0, 2, 32), 0);
    for revision in 2..=255 {
        assert_eq!(valid(SIGNATURE, revision, 36, 0, 0, 0, 4096, 2, 52), 1);
    }
    assert_eq!(valid(SIGNATURE, 1, 36, 0, 0, 0, 4096, 2, 52), 0);
    for length in [0, 19, 20, 35, 37, u32::MAX] {
        assert_eq!(valid(SIGNATURE, 2, length, 0, 0, 0, 4096, 2, 52), 0);
    }
}

// ====== checksum separati obbligatori e tutti i bit della firma ===== //

#[test]
fn checksums_and_signature() {
    for bit in 0..64 {
        assert_eq!(valid(SIGNATURE ^ (1 << bit), 2, 36, 0, 0, 0, 4096, 2, 52), 0);
    }
    for sum in 1..=255 {
        assert_eq!(valid(SIGNATURE, 2, 36, sum, 0, 0, 4096, 2, 52), 0);
        assert_eq!(valid(SIGNATURE, 2, 36, 0, sum, 0, 4096, 2, 52), 0);
    }
}

// ====== indirizzi fisici limitati alla capacita CPU senza richiedere allineamenti inventati ===== //

#[test]
fn physical_limits() {
    for bits in 32..=52 {
        let limit = 1u64 << bits;
        assert_eq!(valid(SIGNATURE, 2, 36, 0, 0, 0, limit - 1, 2, bits), 1);
        assert_eq!(valid(SIGNATURE, 2, 36, 0, 0, 0, limit, 2, bits), 0);
    }
    assert_eq!(valid(SIGNATURE, 2, 36, 0, 0, 0, 0, 2, 52), 0);
    for bits in [0, 31, 53, u32::MAX] {
        assert_eq!(valid(SIGNATURE, 2, 36, 0, 0, 0, 4096, 2, bits), 0);
    }
}

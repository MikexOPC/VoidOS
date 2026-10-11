#[path = "../../../../../../../../../arch/x86/boot/uefi/handoff/transition/rust/validation/capsule_profile.rs"]
mod profile;
use profile::{boot_capsule_check_addresses as addresses, boot_capsule_expected_byte as byte};

fn check(code: u64, entry: u64, top: u64, contract: u64) -> u32 { addresses(code, entry, top, contract, 65536) }

// ====== oracolo indipendente dei salti e limiti estremi ====== //

#[test]
fn relative_handlers_stay_inside_capsule() {
    for vector in 0..32 {
        let base = 64 + vector * 16;
        assert_eq!(byte(base), 0xb8);
        assert_eq!(byte(base + 1), vector);
        assert_eq!(byte(base + 5), 0xe9);
        let displacement = (0..4).fold(0, |value, n| value | (byte(base + 6 + n) << (n * 8)));
        assert_eq!(base + 10 + displacement, 576);
    }
    assert_eq!(byte(579), 0xfd);
    assert_eq!(byte(580), 0xcc);
    assert_eq!(byte(831), 256);
    assert_eq!(byte(u32::MAX), 256);
}

#[test]
fn rejects_overflow_alias_and_bad_alignment() {
    assert_eq!(check(0x100000, 0x200000, 0x400000, 0x500000), 1);
    for code in [0, 4095, 4097, u64::MAX, (1 << 47) - 4095, 1 << 47] {
        assert_eq!(check(code, 0x200000, 0x400000, 0x500000), 0);
    }
    for top in [0, 65536, 0x400001, u64::MAX, 1 << 47] {
        assert_eq!(check(0x100000, 0x200000, top, 0x500000), 0);
    }
    for entry in [0, u64::MAX, 0x100000, 0x3f0000, 0x500000] {
        assert_eq!(check(0x100000, entry, 0x400000, 0x500000), 0);
    }
    assert_eq!(check(0x100000, 0x200000, 0x400000, 0x100000), 0);
    assert_eq!(check(0x3ff000, 0x200000, 0x400000, 0x500000), 0);
    assert_eq!(check(0x100000, 0x200000, 0x400000, 0x3ff000), 0);
    assert_eq!(check((1 << 47) - 4096, 0x200000, 0x400000, 0x500000), 1);
    for bytes in [0, 1, 4095, 65537, u64::MAX] {
        assert_eq!(addresses(0x100000, 0x200000, 0x400000, 0x500000, bytes), 0);
    }
    assert_eq!(addresses(0x100000, 0x200000, 0x400000, 0x500000, 4096), 1);
}

// ====== indirizzi rilocati senza cambiare la destinazione dei salti ====== //

#[test]
fn reproducible_relocations_keep_targets_in_owned_page() {
    for page in 1..=1024u64 {
        let base = page * 4096;
        assert_eq!(addresses(base, 0x2000000, 0x4000000, 0x5000000, 65536), 1);
        for vector in 0..32u64 {
            let offset = 64 + vector * 16;
            let displacement = 576 - (offset + 10);
            assert_eq!(base + offset + 10 + displacement, base + 576);
            assert!(base + offset < base + 4096);
        }
    }
}

// ====== rifiuto dei profili CPU non gestiti senza modificare registri ====== //

#[test]
fn activation_rejects_pcid_cet_la57_and_bad_roots() {
    let check = profile::boot_activation_check_state;
    assert_eq!(check(0x20, 0x100000, 40), 1);
    for bit in [12, 17, 23, 24] { assert_eq!(check(0x20 | (1 << bit), 0x100000, 40), 0); }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(check(0x20, 0x100000, bits), 0); }
    for root in [0, 4095, 4097, 1 << 40, u64::MAX] { assert_eq!(check(0x20, root, 40), 0); }
}

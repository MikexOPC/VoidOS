#[path = "../../../../../../arch/x86/mm/devices/rust/encoding/validation.rs"]
mod validation;

// ====== bordi fisici e shift validati prima dell encoding ===== //

#[test]
fn physical_bounds() {
    for bits in 0..64 {
        assert_eq!(validation::x86_lapic_pte(4096,0x18,bits) != 0,(32..=52).contains(&bits));
    }
    for bits in 32..=52 {
        assert_ne!(validation::x86_lapic_pte((1u64<<bits)-4096,0x18,bits),0);
        assert_eq!(validation::x86_lapic_pte(1u64<<bits,0x18,bits),0);
    }
    assert_eq!(validation::x86_lapic_pte(0,0x18,36),0);
    assert_eq!(validation::x86_lapic_pte(4097,0x18,36),0);
}

// ====== niente user executable global huge o flag cache estranei ===== //

#[test]
fn cache_encoding() {
    for cache in 0..512 {
        let encoded = validation::x86_lapic_pte(0xfee00000,cache,36);
        assert_eq!(encoded != 0,cache & !0x98 == 0);
        if encoded != 0 { assert_eq!(encoded,0x80000000fee00003 | cache); }
    }
}

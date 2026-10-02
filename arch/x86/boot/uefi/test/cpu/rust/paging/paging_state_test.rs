#[path = "../../../../../../lib/processor/rust/paging/paging_state.rs"]
mod paging_state;
use paging_state::x86_validate_paging_state as validate;

const CR0: u64 = (1 << 31) | 1;
const CR4: u64 = 1 << 5;
const EFER: u64 = (1 << 8) | (1 << 10);

// ====== stato obbligatorio e registri che non devono essere confusi ====== //

#[test]
fn requires_active_four_level_long_mode() {
    assert_eq!(validate(CR0, 0x800000, CR4, EFER, 52), 0x80000000);
    for (cr0, cr4, efer) in [
        (CR0 & !(1 << 31), CR4, EFER),
        (CR0 & !1, CR4, EFER),
        (CR0, 0, EFER),
        (CR0, CR4 | (1 << 12), EFER),
        (CR0, CR4, EFER & !(1 << 8)),
        (CR0, CR4, EFER & !(1 << 10)),
    ] {
        assert_eq!(validate(cr0, 0x800000, cr4, efer, 52), 0);
    }
}

#[test]
fn reports_nxe_wp_and_pcid_independently() {
    for flags in 0u32..8 {
        let cr0 = CR0 | if flags & 2 != 0 { 1 << 16 } else { 0 };
        let cr4 = CR4 | if flags & 4 != 0 { 1 << 17 } else { 0 };
        let efer = EFER | if flags & 1 != 0 { 1 << 11 } else { 0 };
        assert_eq!(validate(cr0, 0x800fff, cr4, efer, 52), 0x80000000 | flags);
    }
}

#[test]
fn bounds_physical_root_and_accepts_low_metadata() {
    for bits in 0..=255 {
        if (32..=52).contains(&bits) {
            assert_ne!(validate(CR0, (1u64 << bits) - 1, CR4, EFER, bits), 0);
            assert_eq!(validate(CR0, 1u64 << bits, CR4, EFER, bits), 0);
        } else {
            assert_eq!(validate(CR0, 0x800000, CR4, EFER, bits), 0);
        }
    }
    assert_ne!(validate(CR0, 0, CR4, EFER, 52), 0);
    assert_eq!(validate(CR0, 1 << 63, CR4, EFER, 52), 0);
}

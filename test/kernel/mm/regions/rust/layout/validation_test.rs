#[path = "../../../../../../arch/x86/mm/regions/rust/layout/validation.rs"]
mod validation;
use validation::*;

// ====== basi distinte guardie e handle ammessi solo all'inizio del blocco ===== //

#[test]
fn layout_and_handles() {
    for slot in 0..4u32 {
        let base = 0x700000081000 + u64::from(slot) * 34 * 4096;
        assert_eq!(x86_region_base(slot), base);
        assert_eq!(x86_region_slot(base), slot + 1);
        for offset in 1..34 * 4096 { assert_eq!(x86_region_slot(base + offset), 0); }
        assert_eq!(x86_region_slot(base - 4096), 0);
    }
    for slot in [4, 5, u32::MAX] { assert_eq!(x86_region_base(slot), 0); }
    for base in [0, 0x700000080000, u64::MAX] { assert_eq!(x86_region_slot(base), 0); }
}

// ====== intervalli fisici bounded e limiti CPU verificati senza puntatori ===== //

#[test]
fn frame_bounds() {
    for bits in 32..=52 {
        for pages in 1..=32 {
            let base = (1u64 << bits) - pages * 4096;
            assert_eq!(x86_region_frame(base, pages, bits), pages * 4096);
            assert_eq!(x86_region_frame(base + 4096, pages, bits), 0);
        }
    }
    for pages in [0, 33, u64::MAX] { assert_eq!(x86_region_frame(4096, pages, 52), 0); }
    for base in [0, 4097, u64::MAX - 4095] { assert_eq!(x86_region_frame(base, 1, 52), 0); }
    for bits in [0, 31, 53, 64] { assert_eq!(x86_region_frame(4096, 1, bits), 0); }
}

#[path = "../../../../../mm/physical/rust/alignment/placement.rs"]
mod placement;
use placement::kernel_physical_aligned_base as place;

// ====== modello enumerativo indipendente dalla formula di arrotondamento ===== //

#[test]
fn placement_matches_page_enumeration() {
    for start in 1..=128u64 {
        for available in 1..=16u64 {
            for requested in 1..=17u64 {
                for alignment in [1, 2, 4, 8, 16, 32, 64, 128] {
                    let expected = (start..start + available)
                        .find(|p| p % alignment == 0 && p + requested <= start + available)
                        .map_or(0, |p| p * 4096);
                    assert_eq!(place(start * 4096, available, requested, alignment, 32), expected);
                }
            }
        }
    }
}

// ====== limiti CPU estremi e operazioni aritmetiche che non devono avvolgersi ===== //

#[test]
fn rejects_invalid_and_overflowing_requests() {
    for alignment in [0, 3, 5, u64::MAX, 1 << 63] {
        assert_eq!(place(4096, 64, 1, alignment, 52), 0);
    }
    for bits in [0, 31, 53, 64] { assert_eq!(place(4096, 64, 1, 1, bits), 0); }
    for (base, available, requested) in [
        (0, 64, 1), (4097, 64, 1), (4096, 0, 1), (4096, 64, 0),
        (4096, u64::MAX, 1), (4096, 64, u64::MAX), (u64::MAX - 4095, 1, 1),
    ] { assert_eq!(place(base, available, requested, 1, 52), 0); }
    for bits in 32..=52 {
        let end = 1u64 << bits;
        assert_eq!(place(end - 4096, 1, 1, 1, bits), end - 4096);
        assert_eq!(place(end - 4096, 1, 1, 2, bits), 0);
        assert_eq!(place(end - 4096, 2, 1, 1, bits), 0);
        assert_eq!(place(end - 8192, 2, 2, 2, bits), end - 8192);
        assert_eq!(place(4096, 64, 1, end / 4096, bits), 0);
    }
}

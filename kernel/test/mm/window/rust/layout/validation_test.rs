#[path = "../../../../../../arch/x86/mm/window/rust/layout/validation.rs"]
mod validation;
use validation::*;

// ====== budget e dimensione esplicita della finestra ===== //

#[test]
fn rejects_invalid_page_counts() {
    for pages in [0, 17, 512, 1 << 52, u64::MAX] {
        assert_eq!(x86_window_span(pages), 0);
    }
    for pages in 1..=16 {
        assert_eq!(x86_window_span(pages), pages * 4096);
    }
}

// ====== ogni larghezza fisica e ogni conteggio ammesso ===== //

#[test]
fn accepts_exact_cpu_end_and_rejects_crossing() {
    for bits in 32..=52 {
        let limit = 1u64 << bits;
        for pages in 1..=16 {
            let bytes = pages * 4096;
            assert_eq!(x86_window_frame(limit - bytes, pages, bits), bytes);
            assert_eq!(x86_window_frame(limit - bytes + 4096, pages, bits), 0);
        }
    }
}

// ====== allineamento base nulla overflow e larghezze non ammesse ===== //

#[test]
fn rejects_misalignment_and_invalid_widths() {
    for bits in [0, 31, 53, 63, 64, u32::MAX] {
        assert_eq!(x86_window_frame(4096, 1, bits), 0);
    }
    for offset in 1..4096 {
        assert_eq!(x86_window_frame(4096 + offset, 1, 52), 0);
    }
    assert_eq!(x86_window_frame(0, 1, 52), 0);
    assert_eq!(x86_window_frame(u64::MAX - 4095, 1, 52), 0);
    assert_eq!(x86_window_frame(4096, u64::MAX, 52), 0);
}

// ====== confronto deterministico con aritmetica piu larga ===== //

#[test]
fn matches_independent_u128_model() {
    let mut state = 0xd132_4576_cafe_9103u64;
    for _ in 0..8192 {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
        let base = (state >> 12) & !4095;
        let pages = (state >> 48) & 31;
        let bits = 30 + (state as u32 % 26);
        let end = u128::from(base) + u128::from(pages) * 4096;
        let valid = (32..=52).contains(&bits) && base != 0 && (1..=16).contains(&pages)
            && end <= 1u128 << bits;
        assert_eq!(x86_window_frame(base, pages, bits), if valid { pages * 4096 } else { 0 });
    }
}

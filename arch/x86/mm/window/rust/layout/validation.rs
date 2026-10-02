#![cfg_attr(not(test), no_std)]

const PAGE_BYTES: u64 = 4096;
const WINDOW_PAGES: u64 = 16;

// ====== dimensione verificata prima di qualsiasi allocazione ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_window_span(pages: u64) -> u64 {
    if !(1..=WINDOW_PAGES).contains(&pages) {
        return 0;
    }
    pages.checked_mul(PAGE_BYTES).unwrap_or(0)
}

// ====== estremi del frame contiguo entro la larghezza fisica CPU ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_window_frame(base: u64, pages: u64, bits: u32) -> u64 {
    let bytes = x86_window_span(pages);
    if bytes == 0 || base < PAGE_BYTES || base & (PAGE_BYTES - 1) != 0
        || !(32..=52).contains(&bits) {
        return 0;
    }
    match base.checked_add(bytes) {
        Some(end) if end <= 1u64 << bits => bytes,
        _ => 0,
    }
}

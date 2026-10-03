#![cfg_attr(not(test), no_std)]
const GUARD: u64 = 0x700000080000;
const STRIDE: u64 = 34 * 4096;

// ====== geometria fissa bounded nello stesso ramo PT di bootstrap ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_region_base(slot: u32) -> u64 {
    if slot < 4 { GUARD + u64::from(slot) * STRIDE + 4096 } else { 0 }
}

// ====== handle solo alla base esatta nessun arrotondamento di puntatori arbitrari ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_region_slot(address: u64) -> u32 {
    match address.checked_sub(GUARD + 4096) {
        Some(offset) if offset < 4 * STRIDE && offset % STRIDE == 0 => (offset / STRIDE) as u32 + 1,
        _ => 0,
    }
}

// ====== blocco contiguo di una a trentadue pagine entro il limite CPU ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_region_frame(base: u64, pages: u64, bits: u32) -> u64 {
    if !(32..=52).contains(&bits) || base < 4096 || base & 4095 != 0 || !(1..=32).contains(&pages) { return 0; }
    let bytes = pages * 4096;
    match base.checked_add(bytes) { Some(end) if end <= (1u64 << bits) => bytes, _ => 0 }
}

#![cfg_attr(not(test), no_std)]

// ====== candidato fisico allineato senza puntatori heap o aritmetica unchecked ===== //

fn candidate(base: u64, available: u64, pages: u64, alignment: u64, bits: u32) -> Option<u64> {
    if !(32..=52).contains(&bits) || base < 4096 || base & 4095 != 0
        || available == 0 || pages == 0 || !alignment.is_power_of_two() { return None; }
    let limit = 1u64 << bits;
    let alignment_bytes = alignment.checked_mul(4096)?;
    if alignment_bytes > limit { return None; }
    let end = base.checked_add(available.checked_mul(4096)?)?;
    if end > limit { return None; }
    let mask = alignment_bytes - 1;
    let aligned = base.checked_add(mask)? & !mask;
    let requested_end = aligned.checked_add(pages.checked_mul(4096)?)?;
    if requested_end <= end { Some(aligned) } else { None }
}

// ====== FFI scalare zero significa nessun candidato e non una pagina assegnabile ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_physical_aligned_base(base: u64, available: u64, pages: u64,
                                              alignment_pages: u64, bits: u32) -> u64 {
    candidate(base, available, pages, alignment_pages, bits).unwrap_or(0)
}

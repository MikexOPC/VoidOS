#![cfg_attr(not(test), no_std)]

// ====== intervalli fisici con moltiplicazione e somma controllate ====== //

fn end(base: u64, pages: u64) -> Option<u64> {
    if base & 4095 != 0 || pages == 0 { return None; }
    let bytes = pages.checked_mul(4096)?;
    let limit = base.checked_add(bytes)?;
    if limit > (1u64 << 52) { None } else { Some(limit) }
}

#[unsafe(no_mangle)]
pub extern "C" fn kernel_region_valid(base: u64, pages: u64, _attributes: u64, _kind: u32) -> u32 {
    u32::from(end(base, pages).is_some())
}

// ====== solo ConventionalMemory ordinaria candidata per un futuro allocatore ====== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_region_candidate(attributes: u64, kind: u32) -> u32 {
    // Le capacita cache possono coesistere: non rappresentano tutte impostazioni attive.
    u32::from(kind == 7 && attributes & !0x1f == 0)
}

// ====== confronto conservativo anche per input invalidi ====== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_regions_overlap(first: u64, pages: u64, second: u64, other: u64) -> u32 {
    match (end(first, pages), end(second, other)) {
        (Some(a), Some(b)) => u32::from(first < b && second < a),
        _ => 1,
    }
}

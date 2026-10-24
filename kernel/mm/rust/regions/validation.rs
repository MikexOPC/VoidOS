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

// ====== attributi ammessi, identici al loader (lib/firmware/uefi/memory.h) ====== //

// UEFI 2.10 7.2 EFI_MEMORY_DESCRIPTOR: UC/WC/WT/WB/UCE bit 0-4 sono capacita cache; XP
// (EFI_MEMORY_XP 0x4000) e solo informativo per il kernel, che impone NX con il proprio
// paging. Su firmware reale la ConventionalMemory porta spesso XP (B8).
pub const KERNEL_CONVENTIONAL_ATTRIBUTES: u64 = 0x1f | 0x4000;

// ====== solo ConventionalMemory ordinaria candidata per un futuro allocatore ====== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_region_candidate(attributes: u64, kind: u32) -> u32 {
    // Le capacita cache possono coesistere: non rappresentano tutte impostazioni attive.
    u32::from(kind == 7 && attributes & !KERNEL_CONVENTIONAL_ATTRIBUTES == 0)
}

// ====== base utilizzabile: la pagina zero non entra mai nel pool (B8, M2) ====== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_physical_base(base: u64) -> u64 {
    if base == 0 { 4096 } else { base }
}

// ====== limite CPU e capacita WB richiesti per ownership fisica ====== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_physical_pages(base: u64, pages: u64, attributes: u64, kind: u32, bits: u32) -> u64 {
    if !(32..=52).contains(&bits) || kind != 7 || attributes & 8 == 0
        || attributes & !KERNEL_CONVENTIONAL_ATTRIBUTES != 0 { return 0; }
    // Una regione che parte da 0 perde solo la pagina zero, non l'intero blocco.
    let usable = if base == 0 { pages.saturating_sub(1) } else { pages };
    if usable == 0 { return 0; }
    match end(kernel_physical_base(base), usable) {
        Some(limit) if limit <= (1u64 << bits) => usable,
        _ => 0,
    }
}

// ====== confronto conservativo anche per input invalidi ====== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_regions_overlap(first: u64, pages: u64, second: u64, other: u64) -> u32 {
    match (end(first, pages), end(second, other)) {
        (Some(a), Some(b)) => u32::from(first < b && second < a),
        _ => 1,
    }
}

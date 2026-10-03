#![cfg_attr(not(test), no_std)]

// ====== modalita ereditata senza scrivere IA32 APIC BASE ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_apic_mode(base: u64, ecx: u32, edx: u32, bits: u32) -> u32 {
    let required = (1 << 5) | (1 << 9) | (1 << 16);
    if !(32..=52).contains(&bits) || edx & required != required { return 0; }
    // Profilo iniziale: campo indirizzo APIC di 36 bit, comunque entro MAXPHYADDR.
    let mask = ((1u64 << bits.min(36)) - 1) & !4095;
    if base & !(mask | (1 << 8) | (1 << 10) | (1 << 11)) != 0 || base & mask == 0 ||
        base & ((1 << 8) | (1 << 11)) != (1 << 8) | (1 << 11) { return 0; }
    if base & (1 << 10) == 0 { 1 } else if ecx & (1 << 21) != 0 { 2 } else { 0 }
}

// ====== UC forte distinto da UC meno e PA0 WB per i mapping RAM esistenti ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_apic_uc_slot(pat: u64) -> u32 {
    if pat & 255 != 6 { return 0; }
    let mut selected = 0;
    for index in 0..8 {
        let value = (pat >> (index * 8)) & 255;
        if !matches!(value, 0 | 1 | 4 | 5 | 6 | 7) { return 0; }
        if value == 0 && selected == 0 { selected = index + 1; }
    }
    selected
}

// ====== encoding per PTE 4 KiB con PAT al bit 7 ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_apic_cache_bits(index: u32) -> u64 {
    if index > 7 { return u64::MAX; }
    u64::from((index & 1) << 3 | (index & 2) << 3 | (index & 4) << 5)
}

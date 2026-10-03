#![cfg_attr(not(test), no_std)]

// ====== schema RSDP e checksum ricevuti come scalari senza puntatori unsafe ===== //

#[unsafe(no_mangle)]
pub extern "C" fn os_acpi_rsdp_fields(signature: u64, revision: u32, length: u32,
    legacy_sum: u32, extended_sum: u32, rsdt: u64, xsdt: u64, source: u32, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) || signature != 0x2052_5450_2044_5352
        || legacy_sum != 0 || !(1..=2).contains(&source) { return 0; }
    let limit = 1u64 << bits;
    if revision == 0 {
        return u32::from(source == 1 && length == 20 && rsdt >= 4096 && rsdt < limit && xsdt == 0);
    }
    u32::from((2..=255).contains(&revision) && length == 36 && extended_sum == 0
        && xsdt >= 4096 && xsdt < limit && rsdt < limit)
}

#![cfg_attr(not(test), no_std)]

// ====== schema root SDT e conteggio bounded senza sottrazioni premature ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_acpi_root_layout(signature: u32, length: u32, revision: u32, width: u32) -> u32 {
    let expected = match width { 4 => 0x5444_5352, 8 => 0x5444_5358, _ => return 0 };
    if signature != expected || revision != 1 || length < 36 { return 0; }
    let payload = length - 36;
    if payload % width != 0 || payload / width > 256 { return 0; }
    payload / width + 1
}

// ====== intervallo fisico completo con limite MAXPHYADDR ===== //

#[unsafe(no_mangle)]
pub extern "C" fn kernel_acpi_physical_span(physical: u64, bytes: u32, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) || physical < 4096 || bytes == 0 { return 0; }
    let limit = 1u64 << bits;
    u32::from(physical < limit && u64::from(bytes) <= limit - physical)
}

#![cfg_attr(not(test), no_std)]

// ====== validazione numerica del profilo CPU per le tabelle a quattro livelli ====== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_validate_paging_profile(
    maximum_leaf: u32,
    features: u32,
    address_sizes: u32,
) -> u32 {
    if maximum_leaf < 0x8000_0008 || features & ((1 << 29) | (1 << 20)) != ((1 << 29) | (1 << 20)) {
        return 0;
    }
    let physical_bits = address_sizes & 255;
    let linear_bits = (address_sizes >> 8) & 255;
    if !(32..=52).contains(&physical_bits) || !matches!(linear_bits, 48 | 57) {
        return 0;
    }
    physical_bits
}

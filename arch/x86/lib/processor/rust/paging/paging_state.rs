#![cfg_attr(not(test), no_std)]

// ====== profilo attivo a quattro livelli e metadati di CR3 ====== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_validate_paging_state(
    cr0: u64,
    cr3: u64,
    cr4: u64,
    efer: u64,
    bits: u32,
) -> u32 {
    if !(32..=52).contains(&bits)
        || cr0 & ((1 << 31) | 1) != ((1 << 31) | 1)
        || cr4 & (1 << 5) == 0
        || cr4 & (1 << 12) != 0
        || efer & ((1 << 8) | (1 << 10)) != ((1 << 8) | (1 << 10))
        || cr3 >> bits != 0
    {
        return 0;
    }
    let nx = u32::from(efer & (1 << 11) != 0);
    let wp = u32::from(cr0 & (1 << 16) != 0) << 1;
    let pcid = u32::from(cr4 & (1 << 17) != 0) << 2;
    0x8000_0000 | nx | wp | pcid
}

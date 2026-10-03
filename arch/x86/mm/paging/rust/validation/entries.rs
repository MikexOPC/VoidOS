#![cfg_attr(not(test), no_std)]
const RECURSIVE: u64 = 128;
const ADDRESS: u64 = 0x000f_ffff_ffff_f000;
const NX: u64 = 1 << 63;

// ====== formato ristretto del bootstrap a quattro livelli e pagine da 4 KiB ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_entry_valid(entry: u64, level: u32, bits: u32) -> u32 {
    if level > 3 || !(32..=52).contains(&bits) { return 0; }
    if entry == 0 { return 1; }
    let physical = entry & ADDRESS;
    if physical == 0 || physical >= (1u64 << bits) { return 0; }
    let flags = entry & !ADDRESS & !0x60;
    if level == 3 {
        u32::from(flags == 1 || flags == (1 | NX) || flags == (3 | NX))
    } else {
        u32::from(flags == 3)
    }
}

// ====== vista del figlio calcolata senza dereferenziare indirizzi o usare unsafe ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_child_view(parent: u64, level: u32, slot: u32) -> u64 {
    if level > 2 || slot >= 512 || parent & 4095 != 0 || parent >> 39 != RECURSIVE {
        return 0;
    }
    let indices = [(parent >> 30) & 511, (parent >> 21) & 511, (parent >> 12) & 511];
    for index in 0..(3 - level as usize) {
        if indices[index] != RECURSIVE { return 0; }
    }
    if level != 0 && (indices[(3 - level) as usize] >= 256
        || indices[(3 - level) as usize] == RECURSIVE) { return 0; }
    if level == 0 && (slot >= 256 || u64::from(slot) == RECURSIVE) { return 0; }
    (RECURSIVE << 39) | ((parent << 9) & ((1u64 << 39) - 1)) | (u64::from(slot) << 12)
}

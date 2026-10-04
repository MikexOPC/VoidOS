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

// ====== indirizzo foglia ricostruito dalla vista PT prima della classificazione ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_leaf_virtual(view: u64, slot: u32) -> u64 {
    let a = (view >> 30) & 511;
    if view >> 39 != RECURSIVE || view & 4095 != 0 || slot >= 512 || a >= 256 || a == RECURSIVE {
        return 0;
    }
    (a << 39) | (((view >> 21) & 511) << 30) | (((view >> 12) & 511) << 21) | (u64::from(slot) << 12)
}

// ====== un solo slot MMIO autorizzato senza accettare alias WB ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_leaf_kind(entry: u64, address: u64, physical: u64, cache: u64, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) { return 0; }
    if address == 0x0000_7000_0012_0000 {
        if physical == 0 || physical & 4095 != 0 || physical >= (1u64 << bits) || cache & !0x98 != 0 {
            return 0;
        }
        return if (entry & !0x60) == (physical | cache | NX | 3) { 2 } else { 0 };
    }
    if physical != 0 && entry & ADDRESS == physical { return 0; }
    x86_paging_entry_valid(entry,3,bits)
}

#![cfg_attr(not(test), no_std)]
// K1: X86_LAYOUT_RECURSIVE_SLOT in arch/x86/configs/layout.h, checked by Assert-LayoutCopies.
const RECURSIVE: u64 = 510;
const ADDRESS: u64 = 0x000f_ffff_ffff_f000;
const NX: u64 = 1 << 63;
// K1: X86_FB_VIRTUAL (layout.h framebuffer slot + 4096), checked by Assert-LayoutCopies.
const VIDEO: u64 = 0xFFFF_F080_0000_1000;

// Intel SDM Vol. 3A 3.3.7.1: slots 256-511 are the upper half, bits 63:48 copy bit 47.
fn slot_address(slot: u64) -> u64 {
    (slot << 39) | if slot >= 256 { 0xFFFF_0000_0000_0000 } else { 0 }
}

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
    if level > 2 || slot >= 512 || parent & 4095 != 0 || (parent >> 39) & 511 != RECURSIVE {
        return 0;
    }
    let indices = [(parent >> 30) & 511, (parent >> 21) & 511, (parent >> 12) & 511];
    for index in indices.iter().take(3 - level as usize) {
        if *index != RECURSIVE { return 0; }
    }
    // K1: kernel slots may be in either half; only the recursive slot is never descended.
    if level != 0 && indices[(3 - level) as usize] == RECURSIVE { return 0; }
    if level == 0 && u64::from(slot) == RECURSIVE { return 0; }
    slot_address(RECURSIVE) | ((parent << 9) & ((1u64 << 39) - 1)) | (u64::from(slot) << 12)
}

// ====== indirizzo foglia ricostruito dalla vista PT prima della classificazione ===== //

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_leaf_virtual(view: u64, slot: u32) -> u64 {
    let a = (view >> 30) & 511;
    if (view >> 39) & 511 != RECURSIVE || view & 4095 != 0 || slot >= 512 || a == RECURSIVE {
        return 0;
    }
    slot_address(a) | (((view >> 21) & 511) << 30) | (((view >> 12) & 511) << 21) | (u64::from(slot) << 12)
}

// ====== un solo slot MMIO autorizzato senza accettare alias WB ===== //

// K1: X86_LAPIC_VIRTUAL (layout.h windows slot + 0x120000), checked by Assert-LayoutCopies.
const LAPIC_VIRTUAL: u64 = 0xFFFF_F400_0012_0000;

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_leaf_kind(entry: u64, address: u64, physical: u64, cache: u64, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) { return 0; }
    if address == LAPIC_VIRTUAL {
        if physical == 0 || physical & 4095 != 0 || physical >= (1u64 << bits) || cache & !0x98 != 0 {
            return 0;
        }
        return if (entry & !0x60) == (physical | cache | NX | 3) { 2 } else { 0 };
    }
    if physical != 0 && entry & ADDRESS == physical { return 0; }
    x86_paging_entry_valid(entry,3,bits)
}

// ====== x86_paging_video_policy ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_video_policy(address: u64, physical: u64, pages: u64,
    aperture_pages: u64, cache: u64, pat: u64, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) || address != VIDEO || physical == 0 ||
        physical & 4095 != 0 || !(1..=4321).contains(&pages) ||
        !(pages..=8193).contains(&aperture_pages) || cache & !0x98 != 0 {
        return 0;
    }
    let Some(end) = physical.checked_add(aperture_pages * 4096) else { return 0; };
    if end > 1u64 << bits || pat & 255 != 6 { return 0; }
    for slot in 0..8 {
        if !matches!((pat >> (slot * 8)) & 255, 0 | 1 | 4 | 5 | 6 | 7) { return 0; }
    }
    let index = ((cache >> 5) & 4) | ((cache >> 3) & 3);
    // Intel SDM Vol. 3A Section 12.12.2: framebuffer accepts WC (0x01) or UC (0x00).
    u32::from(matches!((pat >> (index * 8)) & 255, 0 | 1))
}

// ====== x86_paging_video_leaf ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_paging_video_leaf(entry: u64, address: u64, physical: u64,
    virtual_base: u64, pages: u64, aperture_pages: u64, cache: u64, bits: u32) -> u32 {
    if !(32..=52).contains(&bits) || virtual_base != VIDEO || physical == 0 ||
        physical & 4095 != 0 || !(1..=4321).contains(&pages) ||
        !(pages..=8193).contains(&aperture_pages) || cache & !0x98 != 0 {
        return 0;
    }
    let Some(end) = physical.checked_add(aperture_pages * 4096) else { return 0; };
    if end > 1u64 << bits { return 0; }
    if address >> 39 == virtual_base >> 39 {
        if address < virtual_base || address & 4095 != 0 { return 0; }
        let offset = address - virtual_base;
        if offset >= pages * 4096 { return 0; }
        return if entry & !0x60 == (physical + offset) | cache | NX | 3 { 3 } else { 0 };
    }
    let frame = entry & ADDRESS;
    if frame >= physical && frame < end { return 0; }
    1
}

#[path = "../../../../../../arch/x86/mm/paging/rust/validation/entries.rs"]
mod entries;
use entries::{x86_paging_child_view as child, x86_paging_entry_valid as valid};
// K1: base of the windows slot (X86_LAYOUT_WINDOWS_VIRTUAL in arch/x86/configs/layout.h).
const WINDOWS: u64 = 0xFFFF_F400_0000_0000;
// K1: recursive slot 510 (X86_LAYOUT_RECURSIVE_SLOT), upper half so sign-extended.
const R: u64 = 510;
const RV: u64 = 0xFFFF_0000_0000_0000 | (R << 39);
const ROOT: u64 = RV | (R << 30) | (R << 21) | (R << 12);

// ====== virtuale LAPIC ricostruito e classificazione scalare senza unsafe ===== //

#[test]
fn mixed_leaf() {
    let pt = child(child(child(ROOT,0,488),1,0),2,0); // K1: windows slot 488
    let address = entries::x86_paging_leaf_virtual(pt,288);
    assert_eq!(address,WINDOWS + 0x12_0000);
    let entry = 0x80000000fee0001b;
    assert_eq!(entries::x86_paging_leaf_kind(entry,address,0xfee00000,0x18,36),2);
    for bit in 0..64 {
        assert_eq!(entries::x86_paging_leaf_kind(entry ^ (1u64<<bit),address,0xfee00000,0x18,36) != 0,
            bit == 5 || bit == 6);
    }
    assert_eq!(entries::x86_paging_leaf_kind(entry,address,0,0,36),0);
    assert_eq!(entries::x86_paging_leaf_kind(0x80000000fee00003,address+4096,0xfee00000,0x18,36),0);
    assert_eq!(entries::x86_paging_leaf_kind(entry,address+4096,0xfee00000,0x18,36),0);
    assert_eq!(entries::x86_paging_leaf_virtual(pt,512),0);
    assert_eq!(entries::x86_paging_leaf_virtual(pt+1,288),0);
    // K1: a leaf in an upper-half slot is rebuilt sign-extended (Intel SDM Vol. 3A 3.3.7.1).
    let upper = child(child(child(ROOT,0,481),1,0),2,0);
    assert_eq!(entries::x86_paging_leaf_virtual(upper,1),0xFFFF_F080_0000_1000);
    let top = child(child(child(ROOT,0,511),1,511),2,511);
    assert_eq!(entries::x86_paging_leaf_virtual(top,511),0xFFFF_FFFF_FFFF_F000);
}

// ====== flags esclusivamente supervisor e nessuna pagina scrivibile eseguibile ===== //

#[test]
fn entry_policy() {
    for bits in 32..=52 {
        for level in 0..4 {
            assert_eq!(valid(0, level, bits), 1);
            let entry = 0x1003 | if level == 3 { 1 << 63 } else { 0 };
            assert_eq!(valid(entry | 0x60, level, bits), 1);
            for bit in [2, 3, 4, 7, 8, 9, 10, 11, 52, 62] {
                assert_eq!(valid(entry | (1 << bit), level, bits), 0);
            }
        }
    }
    assert_eq!(valid(0x1003, 3, 32), 0);
    assert_eq!(valid(0x1001, 3, 32), 1);
    assert_eq!(valid(3, 0, 32), 0);
    assert_eq!(valid((1 << 32) | 3, 0, 32), 0);
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(valid(0x1003, 0, bits), 0); }
}

// ====== formule indipendenti per tutti i livelli e indici di bordo ===== //

#[test]
fn recursive_views() {
    for a in [0, 1, 127, 128, 129, 224, 255, 256, 481, 509, 511] {
        let pdpt = child(ROOT, 0, a);
        assert_eq!(pdpt, RV | (R << 30) | (R << 21) | (u64::from(a) << 12));
        for b in [0, 1, 511] {
            let pd = child(pdpt, 1, b);
            assert_eq!(pd, RV | (R << 30) | (u64::from(a) << 21) | (u64::from(b) << 12));
            for c in [0, 1, 511] {
                assert_eq!(child(pd, 2, c), RV | (u64::from(a) << 30) | (u64::from(b) << 21) | (u64::from(c) << 12));
            }
        }
    }
    // K1: upper-half kernel slots are descended; the recursive slot and indices >= 512 are not.
    for slot in [510, 512, u32::MAX] { assert_eq!(child(ROOT, 0, slot), 0); }
    assert_eq!(child(ROOT + 1, 0, 1), 0);
    assert_eq!(child(ROOT, 3, 1), 0);
    assert_eq!(child(0, 0, 1), 0);
}

// ====== video_uc_policy ======

#[test]
fn video_uc_policy() {
    let base = 0xFFFF_F080_0000_1000; // K1: X86_FB_VIRTUAL, framebuffer slot 481
    let pat = 0x0007_0406_0007_0406;
    for bits in 32..=52 {
        assert_eq!(entries::x86_paging_video_policy(base, 0xc000_0000, 4321, 8193, 0x18, pat, bits), 1);
        assert_eq!(entries::x86_paging_video_policy(base, (1u64 << bits) - 4096, 1, 1, 0x18, pat, bits), 1);
        assert_eq!(entries::x86_paging_video_policy(base, (1u64 << bits) - 4096, 1, 2, 0x18, pat, bits), 0);
    }
    for cache in 0..256 {
        assert_eq!(entries::x86_paging_video_policy(base, 0xc000_0000, 3, 8, cache, pat, 32),
            u32::from(cache == 0x18 || cache == 0x98));
    }
    for slot in 0..8 {
        for encoding in [2, 3, 8, 255] {
            let bad = (pat & !(255u64 << (slot * 8))) | (encoding << (slot * 8));
            assert_eq!(entries::x86_paging_video_policy(base, 0xc000_0000, 3, 8, 0x18, bad, 32), 0);
        }
    }
    for pages in [0, 4322, u64::MAX] {
        assert_eq!(entries::x86_paging_video_policy(base, 0xc000_0000, pages, 8193, 0x18, pat, 32), 0);
    }
    for address in [0, base - 4096, base + 1, base + 4096, RV + 4096] {
        assert_eq!(entries::x86_paging_video_policy(address, 0xc000_0000, 3, 8, 0x18, pat, 32), 0);
    }
    let pat_wc = 0x0007_0401_0007_0406;
    assert_eq!(entries::x86_paging_video_policy(base, 0xc000_0000, 3, 8, 0x80, pat_wc, 32), 1);
}

// ====== video_leaf_permissions_and_guards ======

#[test]
fn video_leaf_permissions_and_guards() {
    let base = 0xFFFF_F080_0000_1000; // K1: X86_FB_VIRTUAL, framebuffer slot 481
    let physical = 0xc000_0000;
    for page in 0..4321 {
        let address = base + page * 4096;
        let entry = (physical + page * 4096) | (1 << 63) | 0x1b;
        assert_eq!(entries::x86_paging_video_leaf(entry, address, physical, base, 4321, 8193, 0x18, 32), 3);
    }
    let entry = physical | (1 << 63) | 0x1b;
    for bit in 0..64 {
        assert_eq!(entries::x86_paging_video_leaf(entry ^ (1 << bit), base, physical, base, 3, 8, 0x18, 32),
            if bit == 5 || bit == 6 { 3 } else { 0 });
    }
    for address in [base - 4096, base + 3 * 4096, base + 1, base + (1 << 21)] {
        assert_eq!(entries::x86_paging_video_leaf(entry, address, physical, base, 3, 8, 0x18, 32), 0);
    }
}

// ====== video_capacity_aliases ======

#[test]
fn video_capacity_aliases() {
    let base = 0xFFFF_F080_0000_1000; // K1: X86_FB_VIRTUAL, framebuffer slot 481
    for page in 0..8193 {
        let entry = (0xc000_0000 + page * 4096) | (1 << 63) | 3;
        assert_eq!(entries::x86_paging_video_leaf(entry, 0x7000_0000_0000, 0xc000_0000,
            base, 4321, 8193, 0x18, 32), 0);
    }
    assert_eq!(entries::x86_paging_video_leaf(0xbfff_f003, 0x7000_0000_0000, 0xc000_0000,
        base, 3, 8, 0x18, 32), 1);
    for bits in [0, 31, 53, u32::MAX] {
        assert_eq!(entries::x86_paging_video_leaf(u64::MAX, u64::MAX, u64::MAX,
            base, u64::MAX, u64::MAX, u64::MAX, bits), 0);
    }
}

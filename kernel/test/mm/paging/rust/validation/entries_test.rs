#[path = "../../../../../../arch/x86/mm/paging/rust/validation/entries.rs"]
mod entries;
use entries::{x86_paging_child_view as child, x86_paging_entry_valid as valid};
const ROOT: u64 = (128 << 39) | (128 << 30) | (128 << 21) | (128 << 12);

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
    for a in [0, 1, 127, 129, 224, 255] {
        let pdpt = child(ROOT, 0, a);
        assert_eq!(pdpt, (128 << 39) | (128 << 30) | (128 << 21) | (u64::from(a) << 12));
        for b in [0, 1, 511] {
            let pd = child(pdpt, 1, b);
            assert_eq!(pd, (128 << 39) | (128 << 30) | (u64::from(a) << 21) | (u64::from(b) << 12));
            for c in [0, 1, 511] {
                assert_eq!(child(pd, 2, c), (128 << 39) | (u64::from(a) << 30) | (u64::from(b) << 21) | (u64::from(c) << 12));
            }
        }
    }
    for slot in [128, 256, 511, 512, u32::MAX] { assert_eq!(child(ROOT, 0, slot), 0); }
    assert_eq!(child(ROOT + 1, 0, 1), 0);
    assert_eq!(child(ROOT, 3, 1), 0);
    assert_eq!(child(0, 0, 1), 0);
}

use super::x86_fb_table_entry as entry;

// ====== parents_and_guards ======

#[test]
fn parents_and_guards() {
    assert_eq!(entry(0, 0, 0xc0000000, 768, 0x18, 32, 0x100000), 0x100003);
    assert_eq!(entry(0, 1, 0xc0000000, 768, 0x18, 32, 0), 0);
    assert_eq!(entry(1, 1, 0xc0000000, 768, 0x18, 32, 0x102000), 0x102003);
    assert_eq!(entry(1, 2, 0xc0000000, 768, 0x18, 32, 0), 0);
    assert_eq!(entry(2, 0, 0xc0000000, 768, 0x18, 32, 0), 0);
    assert_eq!(entry(3, 257, 0xc0000000, 768, 0x18, 32, 0), 0);
}

// ====== all_leaf_boundaries ======

#[test]
fn all_leaf_boundaries() {
    for pages in [1u64, 510, 511, 512, 513, 768, 4321] {
        let count = (pages + 2).div_ceil(512) as u32;
        let mut present = 0;
        for table in 0..count {
            for slot in 0..512 {
                let logical = u64::from(table) * 512 + u64::from(slot);
                let value = entry(table + 2, slot, 0xc0000000, pages, 0x18, 32, 0);
                if logical == 0 || logical > pages { assert_eq!(value, 0); }
                else {
                    assert_eq!(value, (0xc0000000 + (logical - 1) * 4096) | (1 << 63) | 0x1b);
                    present += 1;
                }
            }
        }
        assert_eq!(present, pages);
    }
}

// ====== invalid_scalar_inputs ======

#[test]
fn invalid_scalar_inputs() {
    for pages in [0, 4322, u64::MAX] { assert_eq!(entry(2, 1, 0xc0000000, pages, 0x18, 32, 0), u64::MAX); }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(entry(2, 1, 0xc0000000, 1, 0x18, bits, 0), u64::MAX); }
    for physical in [0, 1, u64::MAX, 1u64 << 32] {
        assert_eq!(entry(2, 1, physical, 1, 0x18, 32, 0), u64::MAX);
    }
    assert_eq!(entry(2, 512, 0xc0000000, 1, 0x18, 32, 0), u64::MAX);
    assert_eq!(entry(u32::MAX, 0, 0xc0000000, 1, 0x18, 32, 0), u64::MAX);
    assert_eq!(entry(2, 1, 0xfffff000, 2, 0x18, 32, 0), u64::MAX);
    assert_eq!(entry(0, 0, 0xc0000000, 1, 0x18, 32, 0), u64::MAX);
    assert_eq!(entry(1, 1, 0xc0000000, 1, 0x18, 32, 0x100000), u64::MAX);
    assert_eq!(entry(2, 1, 0xc0000000, 1, 0x18, 32, 0x100000), u64::MAX);
}

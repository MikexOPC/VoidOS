#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/transition/rust/geometry/entries.rs"]
mod entries;
use entries::{xt_entry, xt_geometry, xt_source_entry};

// ====== boundaries ======

#[test]
fn boundaries() {
    assert_eq!(xt_geometry(4096, 8192, 12288), 1);
    assert_eq!(xt_geometry(4096, 8192, 0xffffc000), 1);
    for table in [0, 1, 4096, 8192, 0xffffd000, u64::MAX] {
        assert_eq!(xt_geometry(4096, 8192, table), 0);
    }
    for entry in [4, 7, 0x80, (1_u64 << 32) | 3, u64::MAX] {
        assert_eq!(xt_source_entry(entry, 32), 0);
    }
    assert_eq!(xt_source_entry(0x700003, 32), 1);
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(xt_source_entry(0, bits), 0); }
}

// ====== exact_leaves ======

#[test]
fn exact_leaves() {
    for table in 0..4 {
        let mut present = 0;
        for index in 0..512 {
            let value = xt_entry(table, index, 0x3000, 0x1000, 0x2000, 0x700003, 510);
            assert_ne!(value, u64::MAX);
            if value & 1 != 0 { present += 1; }
            if table == 3 && index == 1 { assert_eq!(value, 0x1001); }
            if table == 3 && index == 2 { assert_eq!(value, (1 << 63) | 0x2003); }
            if table == 0 && index == 510 { assert_eq!(value, 0); }
        }
        assert_eq!(present, [256, 1, 1, 2][table as usize]);
    }
    assert_eq!(xt_entry(4, 0, 0x3000, 0x1000, 0x2000, 0, 510), u64::MAX);
    assert_eq!(xt_entry(0, 512, 0x3000, 0x1000, 0x2000, 0, 510), u64::MAX);
}

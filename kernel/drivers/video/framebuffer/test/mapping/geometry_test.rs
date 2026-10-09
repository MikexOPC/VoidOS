use super::{mapping_pages, mapping_tables};

// ====== aligned_and_partial_pages ======

#[test]
fn aligned_and_partial_pages() {
    assert_eq!(mapping_pages(0x80000000, 3145728, 3145728, 36), 768);
    assert_eq!(mapping_pages(4096, 4096, 4096, 32), 1);
    assert_eq!(mapping_pages(4100, 4096, 4096, 32), 2);
    assert_eq!(mapping_pages(8188, 4, 4, 32), 1);
    assert_eq!(mapping_pages(8188, 8, 8, 32), 2);
}

// ====== physical_boundary ======

#[test]
fn physical_boundary() {
    for bits in 32..=52 {
        let limit = 1u64 << bits;
        assert_eq!(mapping_pages(limit - 4, 4, 4, bits), 1);
        assert_eq!(mapping_pages(limit - 4, 8, 4, bits), 0);
        assert_eq!(mapping_pages(limit, 4, 4, bits), 0);
    }
    assert_eq!(mapping_pages(u64::MAX - 3, 4, 4, 52), 0);
    for bits in [0, 31, 53, u32::MAX] {
        assert_eq!(mapping_pages(4096, 4096, 4096, bits), 0);
    }
}

// ====== capacity_and_alignment ======

#[test]
fn capacity_and_alignment() {
    for base in [0, 4097, 4098, 4099] {
        assert_eq!(mapping_pages(base, 4096, 4096, 52), 0);
    }
    for (capacity, used) in [(0, 4), (3, 4), (4096, 0), (4096, 3),
        (4096, 4097), (33554433, 4), (33554432, 17694724)] {
        assert_eq!(mapping_pages(4096, capacity, used, 52), 0);
    }
    assert_eq!(mapping_pages(8188, 33554432, 17694720, 52), 4321);
}

// ====== guarded_table_budget ======

#[test]
fn guarded_table_budget() {
    assert_eq!(mapping_tables(0), 0);
    assert_eq!(mapping_tables(510), 3);
    assert_eq!(mapping_tables(511), 4);
    assert_eq!(mapping_tables(768), 4);
    assert_eq!(mapping_tables(4321), 11);
    assert_eq!(mapping_tables(4322), 0);
    assert_eq!(mapping_tables(u64::MAX), 0);
}

// ====== independent_rounding_reference ======

#[test]
fn independent_rounding_reference() {
    for offset in (0..4096).step_by(4) {
        for used in [4, 4092, 4096, 4100, 3145728, 17694720] {
            let pages = mapping_pages(0x80000000 + offset, 33554432, used, 36);
            let last = (u128::from(offset) + u128::from(used) - 1) / 4096;
            assert_eq!(u128::from(pages), last + 1);
            let mut tables = 2;
            let mut slots = 0;
            for _ in 0..pages + 2 {
                if slots == 0 { tables += 1; }
                slots = (slots + 1) % 512;
            }
            assert_eq!(mapping_tables(pages), tables);
        }
    }
}

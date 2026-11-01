#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/lowmemory/rust/geometry/pages.rs"]
mod pages;
use pages::x86_ap_low_geometry as valid;

// ====== all_startup_vectors ======

#[test]
fn all_startup_vectors() {
    for vector in 1..=255 {
        assert_eq!(valid(vector << 12, 1), 1);
    }
}

// ====== bounds_and_overflow ======

#[test]
fn bounds_and_overflow() {
    for base in [0, 1, 4095, 4097, 0xfffff, 0x100000, u64::MAX] {
        assert_eq!(valid(base, 1), 0);
    }
    for pages in [0, 2, u64::MAX] {
        assert_eq!(valid(4096, pages), 0);
    }
}

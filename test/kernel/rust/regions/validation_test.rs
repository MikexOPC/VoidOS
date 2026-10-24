#[path = "../../../../kernel/mm/rust/regions/validation.rs"]
mod validation;
use validation::*;

// ====== estremi fisici e moltiplicazione oltre il limite ====== //

#[test]
fn rejects_invalid_ranges() {
    assert_eq!(kernel_region_valid(0, 1, 0, 7), 1);
    assert_eq!(kernel_region_valid(1, 1, 0, 7), 0);
    assert_eq!(kernel_region_valid(4096, 0, 0, 7), 0);
    assert_eq!(kernel_region_valid(4096, u64::MAX, 0, 7), 0);
    assert_eq!(kernel_region_valid((1 << 52) - 4096, 1, 0, 7), 1);
    assert_eq!(kernel_region_valid((1 << 52) - 4096, 2, 0, 7), 0);
}

#[test]
fn reserves_loader_firmware_and_special_attributes() {
    for kind in 0..=20 { assert_eq!(kernel_region_candidate(8, kind), u32::from(kind == 7)); }
    for bit in 0..64 {
        assert_eq!(kernel_region_candidate(1u64 << bit, 7), u32::from(bit < 5 || bit == 14));
    }
    // B8: firmware ConventionalMemory with XP stays a candidate; RO/RP/runtime do not.
    assert_eq!(kernel_region_candidate(0x400f, 7), 1);
    assert_eq!(kernel_region_candidate(0x2000 | 8, 7), 0);
    assert_eq!(kernel_region_candidate(0x8000_0000_0000_0008, 7), 0);
    assert_eq!(KERNEL_CONVENTIONAL_ATTRIBUTES, 0x401f);
    assert_eq!(kernel_region_candidate(0, 7), 1);
    assert_eq!(kernel_region_candidate(15, 7), 1);
}

#[test]
fn adjacency_is_not_overlap() {
    for index in 1..=1024u64 {
        let base = index * 4096;
        assert_eq!(kernel_regions_overlap(base, 1, base + 4096, 1), 0);
        assert_eq!(kernel_regions_overlap(base, 2, base + 4096, 1), 1);
        assert_eq!(kernel_regions_overlap(base, 1, base, 1), 1);
    }
    assert_eq!(kernel_regions_overlap(0, u64::MAX, 4096, 1), 1);
}

// ====== il budget fisico usa il limite CPU reale e richiede capacita WB ====== //

#[test]
fn allocation_policy_reserves_incompatible_memory() {
    assert_eq!(kernel_physical_pages(4096, 1, 15, 7, 32), 1);
    assert_eq!(kernel_physical_pages(4096, 1, 0, 7, 32), 0);
    assert_eq!(kernel_physical_pages(4096, 1, 8, 2, 32), 0);
    // B8 M2: a region at 0 loses only page zero.
    assert_eq!(kernel_physical_pages(0, 1, 8, 7, 32), 0);
    assert_eq!(kernel_physical_pages(0, 159, 8, 7, 32), 158);
    assert_eq!(kernel_physical_pages(0, 159, 0x400f, 7, 32), 158);
    assert_eq!(kernel_physical_base(0), 4096);
    assert_eq!(kernel_physical_base(8192), 8192);
    assert_eq!(kernel_physical_pages(0, 1 << 20, 8, 7, 32), (1 << 20) - 1);
    assert_eq!(kernel_physical_pages(0, (1 << 20) + 1, 8, 7, 32), 0);
    assert_eq!(kernel_physical_pages(4096, 1, 8 | 0x4000, 7, 32), 1);
    assert_eq!(kernel_physical_pages((1 << 32) - 4096, 1, 8, 7, 32), 1);
    assert_eq!(kernel_physical_pages((1 << 32) - 4096, 2, 8, 7, 32), 0);
    for bits in [0, 31, 53, 64] { assert_eq!(kernel_physical_pages(4096, 1, 8, 7, bits), 0); }
    for bit in 5..64 {
        assert_eq!(kernel_physical_pages(4096, 1, 8 | (1u64 << bit), 7, 52), u64::from(bit == 14));
    }
}

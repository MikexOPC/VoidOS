#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/startup/rust/record/entry.rs"]
mod record;
use record::{Entry, xs_entry_valid};

// ====== valid ======

fn valid() -> Entry {
    Entry { version: 1, bytes: 104, index: 1, apic_id: 7, token: 520,
        code: 4096, data: 8192, transition_root: 0x100000, kernel_root: 0x200000,
        stack_top: 0xffff_f000_0002_1000, descriptors: 0xffff_f000_0004_0000,
        local: 0xffff_ffff_8001_0000, entry: 0xffff_ffff_8010_0000,
        pat: 0x0007_0406_0007_0406, reserved: 0 }
}

// ====== abi_and_valid_widths ======

#[test]
fn abi_and_valid_widths() {
    assert_eq!(core::mem::size_of::<Entry>(), 104);
    for bits in 32..=52 { assert_eq!(xs_entry_valid(valid(), bits), 1); }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(xs_entry_valid(valid(), bits), 0); }
}

// ====== identity_and_token ======

#[test]
fn identity_and_token() {
    for index in [0, 2, 64, u32::MAX] {
        let mut r = valid(); r.index = index;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    for token in [0, 8, 512, 521, 528, u64::MAX, 1u64 << 63] {
        let mut r = valid(); r.token = token;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    for id in [0, 128, 254] {
        let mut r = valid(); r.apic_id = id;
        assert_eq!(xs_entry_valid(r, 48), 1);
    }
    let mut r = valid(); r.apic_id = 255;
    assert_eq!(xs_entry_valid(r, 48), 0);
    r = valid(); r.token = (u64::from(u32::MAX) << 9) | 8;
    assert_eq!(xs_entry_valid(r, 48), 1);
}

// ====== version_size_reserved ======

#[test]
fn version_size_reserved() {
    let mut r = valid(); r.version = 2;
    assert_eq!(xs_entry_valid(r, 48), 0);
    r = valid(); r.bytes = 103;
    assert_eq!(xs_entry_valid(r, 48), 0);
    r = valid(); r.reserved = 1;
    assert_eq!(xs_entry_valid(r, 48), 0);
}

// ====== low_pages_and_aliases ======

#[test]
fn low_pages_and_aliases() {
    for page in [0, 1, 8192, 0x100000, u64::MAX] {
        let mut r = valid(); r.code = page;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    for root in [0, 1, 4096, 8192, 0xffff_d000, 0x1_0000_0000, u64::MAX] {
        let mut r = valid(); r.transition_root = root;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    let mut r = valid(); r.transition_root = 0xffff_c000;
    assert_eq!(xs_entry_valid(r, 32), 1);
    for offset in [0, 4096, 8192, 12288] {
        r = valid(); r.kernel_root = r.transition_root + offset;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    for root in [0, 1, 4096, 8192, 1u64 << 48, u64::MAX] {
        r = valid(); r.kernel_root = root;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
}

// ====== canonical_alignment_overflow_and_virtual_aliases ======

#[test]
fn canonical_alignment_overflow_and_virtual_aliases() {
    for address in [0, 4096, 0x8000_0000_0000, 0xffff_7fff_ffff_ffff, u64::MAX] {
        let mut r = valid(); r.local = address;
        assert_eq!(xs_entry_valid(r, 48), 0);
        r = valid(); r.descriptors = address;
        assert_eq!(xs_entry_valid(r, 48), 0);
        r = valid(); r.stack_top = address;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    for local in [valid().stack_top - 64, valid().descriptors, 0xffff_ffff_ffff_ffc0] {
        let mut r = valid(); r.local = local;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    let mut r = valid(); r.entry = r.stack_top - 1;
    assert_eq!(xs_entry_valid(r, 48), 0);
    r = valid(); r.entry = r.local + 447;
    assert_eq!(xs_entry_valid(r, 48), 0);
    r.entry += 1;
    assert_eq!(xs_entry_valid(r, 48), 1);
}

// ====== emergency_stack_ranges ======

#[test]
fn emergency_stack_ranges() {
    for offset in [0, 8192, 24575, 24576, 32768, 49151, 49152, 57344, 73727] {
        let mut r = valid(); r.entry = r.stack_top + offset;
        assert_eq!(xs_entry_valid(r, 48), 0, "entry offset {offset}");
    }
    for offset in [8192, 32768, 57344] {
        let mut r = valid(); r.descriptors = r.stack_top + offset;
        assert_eq!(xs_entry_valid(r, 48), 0);
        r = valid(); r.local = r.stack_top + offset;
        assert_eq!(xs_entry_valid(r, 48), 0);
    }
    let mut r = valid(); r.stack_top = u64::MAX & !15;
    assert_eq!(xs_entry_valid(r, 48), 0);
    r = valid(); r.entry = r.stack_top + 73728;
    assert_eq!(xs_entry_valid(r, 48), 1);
}

// ====== every_pat_byte ======

#[test]
fn every_pat_byte() {
    for index in 0..8 {
        for byte in 0..=255u64 {
            let mut r = valid();
            r.pat = (r.pat & !(255 << (8 * index))) | (byte << (8 * index));
            let accepted = if index == 0 { byte == 6 } else { matches!(byte, 0 | 1 | 4 | 5 | 6 | 7) };
            assert_eq!(xs_entry_valid(r, 48), u32::from(accepted), "PAT byte {index}: {byte}");
        }
    }
}

#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/startup/rust/record/entry.rs"]
mod record;
#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/startup/rust/return/plan.rs"]
mod return_plan;
use record::Entry;
use return_plan::{Plan, xs_return_valid};

// ====== fixture ======

fn fixture() -> (Entry, Plan) {
    let r = Entry { version: 1, bytes: 104, index: 1, apic_id: 7, token: 520,
        code: 4096, data: 8192, transition_root: 0x100000, kernel_root: 0x200000,
        stack_top: 0xffff_f000_0002_1000, descriptors: 0xffff_f000_0004_0000,
        local: 0xffff_ffff_8001_0000, entry: 0xffff_ffff_8010_0000,
        pat: 0x0007_0406_0007_0406, reserved: 0 };
    let p = Plan { version: 1, bytes: 64, token: r.token, stack: r.stack_top - 1024,
        entry: 0xffff_ffff_8010_2000, idtr: 0xffff_ffff_8010_4000, local: r.local, root: r.kernel_root, reserved: 0 };
    (r, p)
}

// ====== abi_and_bindings ======

#[test]
fn abi_and_bindings() {
    let (r, p) = fixture();
    for bits in 32..=52 { assert_eq!(xs_return_valid(p, r, bits), 1); }
    for field in 0..8 {
        let mut bad = p;
        match field { 0 => bad.version = 0, 1 => bad.bytes = 63, 2 => bad.token += 512,
            3 => bad.stack -= 16, 4 => bad.local += 64, 5 => bad.root += 4096,
            6 => bad.reserved = 1, _ => bad.entry = r.entry }
        assert_eq!(xs_return_valid(bad, r, 48), 0);
    }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(xs_return_valid(p, r, bits), 0); }
}

// ====== aliases ======

#[test]
fn aliases() {
    let (r, p) = fixture();
    for base in [r.stack_top - 65536, r.stack_top - 1, r.descriptors, r.descriptors + 4095, r.local, r.local + 447] {
        let mut bad = p;
        bad.entry = base;
        assert_eq!(xs_return_valid(bad, r, 48), 0);
        bad = p; bad.idtr = base;
        assert_eq!(xs_return_valid(bad, r, 48), 0);
    }
    let mut bad = p;
    bad.idtr = p.entry - 9;
    assert_eq!(xs_return_valid(bad, r, 48), 0);
    bad.idtr = p.entry - 10;
    assert_eq!(xs_return_valid(bad, r, 48), 1);
}

// ====== canonical_and_overflow ======

#[test]
fn canonical_and_overflow() {
    let (r, p) = fixture();
    for value in [0, 4096, 0x0000_8000_0000_0000, 0xffff_7fff_ffff_ffff] {
        let mut bad = p; bad.entry = value;
        assert_eq!(xs_return_valid(bad, r, 48), 0);
        bad = p; bad.idtr = value;
        assert_eq!(xs_return_valid(bad, r, 48), 0);
    }
    let mut edge = p; edge.idtr = u64::MAX - 9;
    assert_eq!(xs_return_valid(edge, r, 48), 1);
    edge.idtr += 1;
    assert_eq!(xs_return_valid(edge, r, 48), 0);
}

// ====== emergency_stack_aliases ======

#[test]
fn emergency_stack_aliases() {
    let (r, p) = fixture();
    for offset in [0, 8192, 24575, 24576, 32768, 49151, 49152, 57344, 73727] {
        let mut bad = p; bad.entry = r.stack_top + offset;
        assert_eq!(xs_return_valid(bad, r, 48), 0);
        bad = p; bad.idtr = r.stack_top + offset;
        assert_eq!(xs_return_valid(bad, r, 48), 0);
    }
    let mut edge = p; edge.idtr = r.stack_top - 65536 - 9;
    assert_eq!(xs_return_valid(edge, r, 48), 0);
    edge.idtr -= 1;
    assert_eq!(xs_return_valid(edge, r, 48), 1);
    edge.idtr = r.stack_top + 73728;
    assert_eq!(xs_return_valid(edge, r, 48), 1);
}

// ====== invalid_record_without_arithmetic_panic ======

#[test]
fn invalid_record_without_arithmetic_panic() {
    let (mut r, p) = fixture();
    r.stack_top = 0;
    assert_eq!(xs_return_valid(p, r, 48), 0);
    r = fixture().0; r.kernel_root = r.transition_root;
    assert_eq!(xs_return_valid(p, r, 48), 0);
}

#[path = "../../../../../../../../../arch/x86/kernel/cpu/percpu/admission/rust/snapshot/policy.rs"]
mod policy;
use policy::*;

// ====== fixture ======

fn fixture() -> (Header, Cpu) {
    (Header { version: 1, bytes: 5160, count: 4, bits: 48, owner: 0, root: 0x200000,
        pat: 0x0007_0406_0007_0406 }, Cpu { index: 1, id: 7, local: 0xffff_ffff_8001_0000,
        wait: 0xffff_ffff_8002_0000,
        expected: Wait { clock: Clock { read: 0xffff_ffff_8010_0000, context: 0xffff_ffff_8002_0020,
            mask: 0xffffff, hz: 3579545 }, ticks: 357955, polls: 1000000 },
        source: Source { physical: 0x700000, length: 276, port: 0x608, mask: 0xffffff, ready: 1 } })
}

// ====== header_bounds ======

#[test]
fn header_bounds() {
    let (h, _) = fixture();
    for count in 2..=64 { let mut v = h; v.count = count; assert_eq!(xag_header_valid(v), 1); }
    for field in 0..8 {
        let mut v = h;
        match field { 0 => v.version = 0, 1 => v.bytes -= 1, 2 => v.count = 1, 3 => v.count = 65,
            4 => v.bits = 31, 5 => v.bits = 53, 6 => v.owner = 8, _ => v.root = 0 }
        assert_eq!(xag_header_valid(v), 0);
    }
    for bits in 32..=52 {
        let mut v = h; v.bits = bits; v.root = (1u64 << bits) - 4096;
        assert_eq!(xag_header_valid(v), 1);
        v.root += 4096; assert_eq!(xag_header_valid(v), 0);
    }
    for byte in 0..8 {
        for value in 0..=255u64 {
            let mut v = h; v.pat = (v.pat & !(255 << (byte * 8))) | (value << (byte * 8));
            assert_eq!(xag_header_valid(v), u32::from(if byte == 0 { value == 6 }
                else { matches!(value, 0 | 1 | 4 | 5 | 6 | 7) }));
        }
    }
}

// ====== cpu_identity_and_pointer_bounds ======

#[test]
fn cpu_identity_and_pointer_bounds() {
    let (_, c) = fixture();
    assert_eq!(xag_cpu_valid(c, 48), 1);
    for index in [0, 1, 63] { let mut v = c; v.index = index; assert_eq!(xag_cpu_valid(v, 48), 1); }
    for field in 0..6 {
        let mut v = c;
        match field { 0 => v.index = 64, 1 => v.id = 255, 2 => v.local = 0,
            3 => v.wait = 4096, 4 => v.expected.clock.context = u64::MAX - 7, _ => v.expected.clock.read = 0 }
        assert_eq!(xag_cpu_valid(v, 48), 0);
    }
    for bits in [0, 31, 53, u32::MAX] { assert_eq!(xag_cpu_valid(c, bits), 0); }
    for field in 0..3 {
        let mut v = c;
        match field { 0 => v.local += 8, 1 => v.wait += 1, _ => v.expected.clock.context += 1 }
        assert_eq!(xag_cpu_valid(v, 48), 0);
    }
}

// ====== aliases ======

#[test]
fn aliases() {
    let (_, c) = fixture();
    let mut v = c; v.wait = c.local + 440; assert_eq!(xag_cpu_valid(v, 48), 0);
    v = c; v.expected.clock.context = c.local; assert_eq!(xag_cpu_valid(v, 48), 0);
    v = c; v.expected.clock.context = c.wait + 24; assert_eq!(xag_cpu_valid(v, 48), 0);
    for address in [c.local, c.local + 447, c.wait, c.wait + 31, c.expected.clock.context + 23] {
        v = c; v.expected.clock.read = address; assert_eq!(xag_cpu_valid(v, 48), 0);
    }
}

// ====== wait_policy ======

#[test]
fn wait_policy() {
    let (_, c) = fixture();
    for field in 0..8 {
        let mut v = c;
        match field { 0 => v.expected.clock.mask = 0, 1 => v.expected.clock.hz = 1000,
            2 => v.expected.ticks = 0, 3 => v.expected.ticks = v.expected.clock.mask / 2,
            4 => v.expected.polls = 0, 5 => v.source.mask = u32::MAX, 6 => v.source.ready = 0,
            _ => v.source.length = 4097 }
        assert_eq!(xag_cpu_valid(v, 48), 0);
    }
    let mut v = c; v.source.mask = u32::MAX; v.expected.clock.mask = u32::MAX;
    assert_eq!(xag_cpu_valid(v, 48), 1);
}

// ====== source_bounds ======

#[test]
fn source_bounds() {
    let (_, c) = fixture();
    for port in [0, 0xfc, 0x609, 0x3f8, 0x3fc, 0x10000, u32::MAX] {
        let mut v = c; v.source.port = port; assert_eq!(xag_cpu_valid(v, 48), 0);
    }
    for bits in 32..=52 {
        let mut v = c; v.source.physical = (1u64 << bits) - u64::from(c.source.length);
        assert_eq!(xag_cpu_valid(v, bits), 1);
        v.source.physical += 1; assert_eq!(xag_cpu_valid(v, bits), 0);
    }
    for physical in [0, 4095, u64::MAX] {
        let mut v = c; v.source.physical = physical; assert_eq!(xag_cpu_valid(v, 48), 0);
    }
}

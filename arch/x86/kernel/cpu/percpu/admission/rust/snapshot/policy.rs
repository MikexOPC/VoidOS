#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Header {
    pub version: u32, pub bytes: u32, pub count: u32, pub bits: u32,
    pub owner: u64, pub root: u64, pub pat: u64,
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Clock { pub read: u64, pub context: u64, pub mask: u32, pub hz: u32 }
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Wait { pub clock: Clock, pub ticks: u32, pub polls: u32 }
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Source { pub physical: u64, pub length: u32, pub port: u32, pub mask: u32, pub ready: u32 }
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Cpu { pub index: u32, pub id: u32, pub local: u64, pub wait: u64, pub expected: Wait, pub source: Source }
const _: () = assert!(size_of::<Header>() == 40 && size_of::<Cpu>() == 80 && align_of::<Cpu>() == 8
    && core::mem::offset_of!(Cpu, expected) == 24 && core::mem::offset_of!(Cpu, source) == 56);

// ====== high ======

fn high(base: u64, bytes: u64, align: u64) -> bool {
    base >= 0xffff_8000_0000_0000 && base & (align - 1) == 0 && base.checked_add(bytes - 1).is_some()
}

// ====== apart ======

fn apart(a: u64, an: u64, b: u64, bn: u64) -> bool {
    if a <= b { b - a >= an } else { a - b >= bn }
}

// ====== xag_header_valid ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xag_header_valid(h: Header) -> u32 {
    if h.version != 1 || h.bytes != 5160 || !(2..=64).contains(&h.count) || !(32..=52).contains(&h.bits)
        || h.owner != 0 || h.root < 4096 || h.root & 4095 != 0 || h.root >= 1u64 << h.bits || h.pat & 255 != 6 { return 0; }
    u32::from(h.pat.to_le_bytes().iter().all(|b| matches!(b, 0 | 1 | 4 | 5 | 6 | 7)))
}

// ====== xag_cpu_valid ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xag_cpu_valid(c: Cpu, bits: u32) -> u32 {
    if c.index >= 64 || c.id >= 255 || !(32..=52).contains(&bits) || !high(c.local, 448, 64)
        || !high(c.wait, 32, 8) || !high(c.expected.clock.context, 24, 8) || !high(c.expected.clock.read, 1, 1)
        || !matches!(c.expected.clock.mask, 0xffffff | 0xffffffff) || c.expected.clock.hz != 3579545
        || c.expected.ticks == 0 || c.expected.ticks >= c.expected.clock.mask / 2 || c.expected.polls == 0
        || c.source.ready != 1 || !(116..=4096).contains(&c.source.length)
        || c.source.physical < 4096 || c.source.physical > (1u64 << bits) - u64::from(c.source.length)
        || c.source.mask != c.expected.clock.mask || !(0x100..=0xfffc).contains(&c.source.port)
        || c.source.port & 3 != 0 || (0x3f8..0x400).contains(&c.source.port) { return 0; }
    let spans = [(c.local, 448), (c.wait, 32), (c.expected.clock.context, 24)];
    for a in 0..spans.len() {
        if !apart(c.expected.clock.read, 1, spans[a].0, spans[a].1) { return 0; }
        for b in a + 1..spans.len() {
            if !apart(spans[a].0, spans[a].1, spans[b].0, spans[b].1) { return 0; }
        }
    }
    1
}

#![deny(unsafe_code)]

const STACK_BYTES: u64 = 65536;
const IST_END: u64 = 73728;
pub const PRIVATE_BYTES: u64 = STACK_BYTES + IST_END;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Entry {
    pub version: u32, pub bytes: u32, pub index: u32, pub apic_id: u32,
    pub token: u64, pub code: u64, pub data: u64, pub transition_root: u64, pub kernel_root: u64,
    pub stack_top: u64, pub descriptors: u64, pub local: u64, pub entry: u64, pub pat: u64, pub reserved: u64,
}
const _: () = assert!(core::mem::size_of::<Entry>() == 104 && core::mem::align_of::<Entry>() == 8);
const _: () = assert!(core::mem::offset_of!(Entry, token) == 16 && core::mem::offset_of!(Entry, transition_root) == 40
    && core::mem::offset_of!(Entry, stack_top) == 56 && core::mem::offset_of!(Entry, entry) == 80
    && core::mem::offset_of!(Entry, reserved) == 96);

// ====== high_span ======

fn high_span(start: u64, bytes: u64, alignment: u64) -> bool {
    start >= 0xffff_8000_0000_0000 && start & (alignment - 1) == 0
        && bytes != 0 && start.checked_add(bytes - 1).is_some()
}

// ====== disjoint ======

fn disjoint(a: u64, an: u64, b: u64, bn: u64) -> bool {
    if a <= b { b - a >= an } else { a - b >= bn }
}

// ====== xs_entry_valid ======

// Intel SDM 253668-078US 4.5.2/4.5.4 and 10.8.5: 4-level root,
// 4-KiB alignment; initial root below 4 GiB. No instruction is executed here.
#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xs_entry_valid(r: Entry, bits: u32) -> u32 {
    if r.version != 1 || r.bytes != 104 || !(1..64).contains(&r.index) || r.apic_id >= 255
        || r.reserved != 0 || !(32..=52).contains(&bits) || r.token & 7 != 0
        || (r.token >> 3) & 63 != u64::from(r.index)
        || !(1..=u64::from(u32::MAX)).contains(&(r.token >> 9)) { return 0; }
    let low = |p: u64| (4096..=0xff000).contains(&p) && p & 4095 == 0;
    if !low(r.code) || !low(r.data) || r.code == r.data || r.transition_root & 4095 != 0
        || !(4096..=0xffff_c000).contains(&r.transition_root)
        || r.kernel_root < 4096 || r.kernel_root & 4095 != 0 || r.kernel_root >= 1u64 << bits { return 0; }
    let physical = [(r.code, 4096), (r.data, 4096), (r.transition_root, 16384), (r.kernel_root, 4096)];
    for a in 0..physical.len() {
        for b in a + 1..physical.len() {
            if !disjoint(physical[a].0, physical[a].1, physical[b].0, physical[b].1) { return 0; }
        }
    }
    if !high_span(r.stack_top, 1, 16) || r.stack_top < 0xffff_8000_0001_0000
        || r.stack_top.checked_add(IST_END).is_none()
        || !high_span(r.descriptors, 4096, 4096) || !high_span(r.local, 448, 64)
        || !high_span(r.entry, 1, 1) { return 0; }
    let virtuals = [(r.stack_top - STACK_BYTES, PRIVATE_BYTES), (r.descriptors, 4096), (r.local, 448), (r.entry, 1)];
    for a in 0..virtuals.len() {
        for b in a + 1..virtuals.len() {
            if !disjoint(virtuals[a].0, virtuals[a].1, virtuals[b].0, virtuals[b].1) { return 0; }
        }
    }
    // PAT encoding: Intel SDM 078US 12.12, Table 12-10; reserved types 2/3 rejected.
    for byte in r.pat.to_le_bytes() {
        if !matches!(byte, 0 | 1 | 4 | 5 | 6 | 7) { return 0; }
    }
    // Initial leaves use PAT index zero, whose policy in K3-H is WB.
    u32::from(r.pat & 255 == 6)
}

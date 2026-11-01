#![deny(unsafe_code)]
use super::record::{Entry, PRIVATE_BYTES, xs_entry_valid};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Plan {
    pub version: u32, pub bytes: u32,
    pub token: u64, pub stack: u64, pub entry: u64, pub idtr: u64, pub local: u64, pub root: u64, pub reserved: u64,
}
const _: () = assert!(core::mem::size_of::<Plan>() == 64 && core::mem::align_of::<Plan>() == 8
    && core::mem::offset_of!(Plan, idtr) == 32 && core::mem::offset_of!(Plan, reserved) == 56);

// ====== high ======

fn high(address: u64, bytes: u64) -> bool {
    address >= 0xffff_8000_0000_0000 && address.checked_add(bytes - 1).is_some()
}

// ====== apart ======

fn apart(a: u64, an: u64, b: u64, bn: u64) -> bool {
    if a <= b { b - a >= an } else { a - b >= bn }
}

// ====== xs_return_valid ======

#[unsafe(no_mangle)]
#[allow(unsafe_code)]
pub extern "C" fn xs_return_valid(p: Plan, r: Entry, bits: u32) -> u32 {
    if xs_entry_valid(r, bits) == 0 || p.version != 1 || p.bytes != 64 || p.token != r.token
        || p.stack != r.stack_top - 1024 || p.local != r.local || p.root != r.kernel_root || p.reserved != 0
        || !high(p.entry, 1) || !high(p.idtr, 10) || p.entry == r.entry
        || !apart(p.entry, 1, p.idtr, 10) { return 0; }
    for (base, bytes) in [(r.stack_top - 65536, PRIVATE_BYTES), (r.descriptors, 4096), (r.local, 448)] {
        if !apart(p.entry, 1, base, bytes) || !apart(p.idtr, 10, base, bytes) { return 0; }
    }
    1
}

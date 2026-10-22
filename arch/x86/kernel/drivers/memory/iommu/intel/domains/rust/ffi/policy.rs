#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#[path="../policy/entries.rs"]
mod entries;

// ====== x86_vtd_domain_next ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_domain_next(cursor:u64,pages:u64,total:u64)->u64 { entries::next(cursor,pages,total) }

// ====== x86_vtd_domain_word ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_domain_word(kind:u32,slot:u32,selector:u32,child:u64,physical:u64,access:u32,bits:u32)->u64 {
    entries::word(kind,slot,selector,child,physical,access,bits)
}

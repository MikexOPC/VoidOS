#![no_std]
#[path="../policy/address.rs"]
mod policy;

// ====== x86_ecam_range_end ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_ecam_range_end(base:u64,first:u32,last:u32,bits:u32)->u64 { policy::range_end(base,first,last,bits) }

// ====== x86_ecam_page ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_ecam_page(base:u64,bus:u32,device:u32,function:u32)->u64 { policy::page(base,bus,device,function) }

// ====== x86_ecam_offset ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_ecam_offset(at:u32)->u32 { policy::offset(at) }

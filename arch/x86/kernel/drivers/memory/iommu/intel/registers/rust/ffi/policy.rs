#![no_std]
#[path="../policy/validation.rs"]
mod policy;

// ====== x86_vtd_entry ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_entry(table:u32,slot:u32,base:u64,cache:u64,child:u64)->u64 {
    policy::entry(table,slot,base,cache,child)
}

// ====== x86_vtd_capability_policy ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_capability_policy(version:u32,cap:u64,ecap:u64,span:u64,status:u32)->u32 {
    policy::capabilities(version,cap,ecap,span,status)
}

// ====== x86_vtd_entry_access ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_entry_access(table:u32,slot:u32,base:u64,cache:u64,child:u64,writable:u32)->u64 {
    policy::entry_access(table,slot,base,cache,child,writable)
}

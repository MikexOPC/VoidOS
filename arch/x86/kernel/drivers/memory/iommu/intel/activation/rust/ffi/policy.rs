#![no_std]
#[path="../policy/commands.rs"]
mod policy;

// ====== x86_vtd_activation_command ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_activation_command(status:u32,bit:u32,set:u32)->u64 { policy::command(status,bit,set) }

// ====== x86_vtd_activation_allowed ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_activation_allowed(offset:u32,width:u32,write:u32,value:u64,root:u64,fro:u32,nfr:u32)->u32 {
    u32::from(policy::allowed(offset,width,write,value,root,fro,nfr))
}

// ====== x86_vtd_fault_decode ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_vtd_fault_decode(high:u64)->u64 { policy::decode(high) }

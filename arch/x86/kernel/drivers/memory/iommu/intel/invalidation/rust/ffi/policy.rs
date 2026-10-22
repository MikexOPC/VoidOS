#![no_std]
#[path="../policy/operations.rs"]
mod policy;

// ====== x86_qi_cache_line ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_qi_cache_line(edx:u32,ebx:u32)->u32 { policy::cache_line(edx,ebx) }

// ====== x86_qi_descriptor ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_qi_descriptor(kind:u32,high:u32,token:u32,status:u64,cap:u64)->u64 {
    policy::descriptor(kind,high,token,status,cap)
}

// ====== x86_qi_register_allowed ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_qi_register_allowed(offset:u32,width:u32,write:u32,value:u64,queue:u64,iro:u32)->u32 {
    u32::from(policy::register_allowed(offset,width,write,value,queue,iro))
}

#![no_std]
#[path="../policy/fields.rs"]
mod policy;

// ====== x86_dmar_header ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_dmar_header(width:u32,flags:u32,revision:u32)->u32 { policy::header(width,flags,revision) }

// ====== x86_dmar_register_span ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_dmar_register_span(base:u64,size:u32,bits:u32)->u64 { policy::register_span(base,size,bits) }

// ====== x86_dmar_scope_fields ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_dmar_scope_fields(kind:u32,length:u32,flags:u32,reserved:u32,enumeration:u32)->u32 {
    policy::scope_fields(kind,length,flags,reserved,enumeration)
}

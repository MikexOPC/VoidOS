#![forbid(unsafe_code)]

// ====== header ======

pub fn header(width:u32,flags:u32,revision:u32)->u32 {
    if !(31..=63).contains(&width) || flags&!7!=0 || revision!=1 { 0 } else { width+1 }
}

// ====== register_span ======

pub fn register_span(base:u64,size:u32,bits:u32)->u64 {
    if !(32..=52).contains(&bits) || size>15 { return 0; }
    let span=4096u64<<size;
    if base<span || base&(span-1)!=0 || base>(1u64<<bits)-span { 0 } else { span }
}

// ====== scope_fields ======

pub fn scope_fields(kind:u32,length:u32,flags:u32,reserved:u32,enumeration:u32)->u32 {
    if !(1..=5).contains(&kind) || flags!=0 || reserved!=0 || !(8..=22).contains(&length) || !length.is_multiple_of(2) ||
        (kind<=2 && enumeration!=0) || (kind>=4 && length!=8) { 0 } else { (length-6)/2 }
}

#[cfg(test)]
#[path="../../../../../../../../../../test/arch/x86/kernel/drivers/memory/iommu/intel/discovery/rust/policy/fields_test.rs"]
mod tests;

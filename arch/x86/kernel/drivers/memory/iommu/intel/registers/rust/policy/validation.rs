#![forbid(unsafe_code)]

// ====== entry ======

pub fn entry(table:u32,slot:u32,base:u64,cache:u64,child:u64)->u64 {
    entry_access(table,slot,base,cache,child,0)
}

// ====== entry_access ======

pub fn entry_access(table:u32,slot:u32,base:u64,cache:u64,child:u64,writable:u32)->u64 {
    if writable>1 || table>2 || slot>=512 || cache&!0x98!=0 { return u64::MAX; }
    if table<2 {
        if slot!=0 { return 0; }
        if child<4096 || child&4095!=0 || child>=1u64<<52 { return u64::MAX; }
        return child|3;
    }
    if slot==0 || slot>15 || slot.is_multiple_of(2) || base==0 { return 0; }
    if base<4096 || base&4095!=0 || base>=1u64<<52 { return u64::MAX; }
    base|cache|(u64::from(writable)<<1)|0x8000000000000001
}

// ====== capabilities ======

pub fn capabilities(version:u32,cap:u64,ecap:u64,span:u64,status:u32)->u32 {
    if version==0 || version&!255!=0 || cap==0 || cap==u64::MAX || ecap==u64::MAX ||
        !(4096..=134217728).contains(&span) || !span.is_power_of_two() { return 0; }
    let fro=((cap>>24)&1023)*16;
    let nfr=((cap>>40)&255)+1;
    let iro=((ecap>>8)&1023)*16;
    if fro+nfr*16>span || iro+16>span { return 0; }
    let bits=((cap>>16)&63)+1;
    let candidate=version>>4==1 && bits>=48 && cap&7<=6 && cap&(1<<10)!=0 && ecap&1!=0 && status==0;
    1+u32::from(candidate)
}

#[cfg(test)]
#[path="../../../../../../../../../../test/arch/x86/kernel/drivers/memory/iommu/intel/registers/rust/policy/validation_test.rs"]
mod tests;

#![forbid(unsafe_code)]

// ====== cache_line ======

pub fn cache_line(edx:u32,ebx:u32)->u32 {
    if edx&((1<<19)|(1<<26))!=((1<<19)|(1<<26)) { return 0; }
    let line=((ebx>>8)&255)*8;
    if (8..=4096).contains(&line) && line.is_power_of_two() { line } else { 0 }
}

// ====== descriptor ======

pub fn descriptor(kind:u32,high:u32,token:u32,status:u64,cap:u64)->u64 {
    if kind>2 || high>1 || token==0 || status<4096 || status&4095!=0 || status>=1u64<<52 { return u64::MAX; }
    match (kind,high) {
        (0,0)=>0x11,
        (1,0)=>0x12|(((cap>>54)&1)<<6)|(((cap>>55)&1)<<7),
        (2,0)=>0x65|(u64::from(token)<<32),
        (2,1)=>status,
        _=>0,
    }
}

#[cfg(test)]
#[path="../../../../../../../../../../test/arch/x86/kernel/drivers/memory/iommu/intel/invalidation/rust/policy/operations_test.rs"]
mod tests;

// ====== register_allowed ======

pub fn register_allowed(offset:u32,width:u32,write:u32,value:u64,queue:u64,iro:u32)->bool {
    if (width!=4 && width!=8) || offset>4096-width || offset&(width-1)!=0 || write>1 || iro>4080 || iro&15!=0 { return false; }
    if write==0 {
        return (width==4 && (offset==0x1c || offset==0x34)) || (width==8 &&
            (offset==0x20 || offset==0x28 || offset==0x80 || offset==0x88 || offset==0x90 || offset==iro+8));
    }
    if width==4 { return offset==0x18 && (value==0 || value==1<<26); }
    if offset==0x88 { return value<4096 && value&15==0; }
    offset==0x90 && (value==0 || (value==queue && queue>=4096 && queue&4095==0 && queue<1u64<<52))
}

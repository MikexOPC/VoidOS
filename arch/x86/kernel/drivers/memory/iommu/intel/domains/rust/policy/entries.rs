#![forbid(unsafe_code)]

// ====== next ======

pub fn next(cursor:u64,pages:u64,total:u64)->u64 {
    if cursor<0x01001000 || cursor&4095!=0 || cursor>=0x03000000 || pages==0 || pages>4100 ||
        total<pages || total>4100 { return 0; }
    match pages.checked_mul(4096).and_then(|n|cursor.checked_add(n)).and_then(|n|n.checked_add(4096)) {
        Some(end) if end<=0x03000000=>end,
        _=>0,
    }
}

// ====== word ======

pub fn word(kind:u32,slot:u32,selector:u32,child:u64,physical:u64,access:u32,bits:u32)->u64 {
    if kind>5 || slot>=512 || !(32..=52).contains(&bits) { return u64::MAX; }
    let valid=|p:u64|p>=4096 && p&4095==0 && p<(1u64<<bits);
    match kind {
        0|1=>{
            if selector>255 || !valid(child) { return u64::MAX; }
            if slot==selector*2 { child|1 }
            else if kind==1 && slot==selector*2+1 { 0x102 }
            else { 0 }
        },
        2|3=>{
            if !valid(child) { return u64::MAX; }
            if slot==0 { child|3 } else { 0 }
        },
        4=>{
            if child==0 { return 0; }
            if !valid(child) || !(8..17).contains(&slot) { return u64::MAX; }
            child|3
        },
        5=>{
            if physical==0 && access==0 { return 0; }
            if !valid(physical) || (access!=1 && access!=3) { return u64::MAX; }
            physical|u64::from(access)
        },
        _=>u64::MAX,
    }
}

#[cfg(test)]
#[path="../../../../../../../../../../test/arch/x86/kernel/drivers/memory/iommu/intel/domains/rust/policy/entries_test.rs"]
mod tests;

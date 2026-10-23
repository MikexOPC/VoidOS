#![forbid(unsafe_code)]

// ====== range_end ======

pub fn range_end(base:u64,first:u32,last:u32,bits:u32)->u64 {
    if !(32..=52).contains(&bits) || first>last || last>255 || base<1<<20 || base&((1<<20)-1)!=0 { return 0; }
    let Some(end)=base.checked_add((u64::from(last)+1)<<20) else { return 0; };
    if end>1u64<<bits { 0 } else { end }
}

// ====== page ======

pub fn page(base:u64,bus:u32,device:u32,function:u32)->u64 {
    if bus>255 || device>31 || function>7 || range_end(base,bus,bus,52)==0 { return 0; }
    let offset=(u64::from(bus)<<20)|(u64::from(device)<<15)|(u64::from(function)<<12);
    base.checked_add(offset).unwrap_or(0)
}

// ====== offset ======

// PCI Local Bus 3.0 6.1 Figure 6-1: ID00, Command/Status04, class08, header0C, BAR0 10h, buses18h.
pub fn offset(at:u32)->u32 { u32::from(matches!(at,0|4|8|12|16|24)) }

// ====== command ======

// PCI Local Bus 3.0 6.2.2 Command register: Memory Space bit1, Bus Master bit2 (Linux v6.18
// pci_regs.h PCI_COMMAND_MEMORY 0x2, PCI_COMMAND_MASTER 0x4). Only those bits may change; the
// result is the 16-bit Command value, so the RW1C Status half is never written back.
pub fn command(old:u32,mask:u32,value:u32)->u32 {
    if mask==0 || mask&!6!=0 || value&!mask!=0 { return u32::MAX; }
    (old&0xffff&!mask)|value
}

#[cfg(test)]
#[path="../../../../../../../../../test/arch/x86/kernel/drivers/bus/pci/ecam/rust/policy/address_test.rs"]
mod tests;

#![forbid(unsafe_code)]
// Intel VT-d5.0 11.4.4.1/2: GCMD/GSTS TE31, SRTP/RTPS30, QIE/QIES26.
pub const TE:u32=1<<31;
pub const SRTP:u32=1<<30;
pub const QIE:u32=1<<26;
// 11.4.4.1 step 2: GSTS & 96FFFFFFh resets the one-shot bits before a new command.
const KEEP:u32=0x96ff_ffff;

// ====== command ======

// Exactly one command per write; only TE set/clear and SRTP set are produced.
pub fn command(status:u32,bit:u32,set:u32)->u64 {
    if set>1 || (bit!=TE && bit!=SRTP) || (bit==SRTP && set==0) || status&QIE==0 { return u64::MAX; }
    let base=status&KEEP;
    u64::from(if set==1 { base|bit } else { base&!bit })
}

// ====== record_slot ======

fn record_slot(offset:u32,fro:u32,nfr:u32)->Option<u32> {
    if offset<fro { return None; }
    let delta=offset-fro;
    let slot=delta/16;
    (slot<nfr).then_some(delta%16)
}

// ====== allowed ======

// 11.4.4.1 GCMD018h, 11.4.5 RTADDR020h (RTA63:12, TTM11:10=00 legacy), 11.4.7.1 FSTS034h
// PFO0 RW1CS, 11.4.7.2 FECTL038h read only, 11.4.7.6 FRCD at FRO*16, F127 RW1CS.
pub fn allowed(offset:u32,width:u32,write:u32,value:u64,root:u64,fro:u32,nfr:u32)->bool {
    if (width!=4 && width!=8) || write>1 || offset>4096-width || offset&(width-1)!=0 ||
        !(1..=256).contains(&nfr) || fro<0x100 || fro&15!=0 || u64::from(fro)+u64::from(nfr)*16>4096 ||
        root<4096 || root&4095!=0 || root>=1u64<<52 { return false; }
    let record=record_slot(offset,fro,nfr);
    if write==0 {
        return (width==4 && matches!(offset,0x1c|0x34|0x38)) ||
            (width==8 && (offset==0x20 || matches!(record,Some(0|8))));
    }
    if width==4 {
        return (offset==0x18 && (value==u64::from(QIE|SRTP) || value==u64::from(QIE|TE) || value==u64::from(QIE))) ||
            (offset==0x34 && value==1);
    }
    (offset==0x20 && (value==root || value==0)) || (record==Some(8) && value==1u64<<63)
}

// ====== decode ======

// 11.4.7.6, upper qword of the record: F127, T1 126 (1=read), FR103:96, SID79:64, reserved91:80 zero.
// Result 0 when F is clear; otherwise bit63 | read<<24 | reason<<16 | sid.
pub fn decode(high:u64)->u64 {
    if high>>63==0 { return 0; }
    if (high>>16)&0xfff!=0 { return u64::MAX; }
    (1u64<<63) | (((high>>62)&1)<<24) | (((high>>32)&255)<<16) | (high&0xffff)
}

#[cfg(test)]
#[path="../../../../../../../../../../test/arch/x86/kernel/drivers/memory/iommu/intel/activation/rust/policy/commands_test.rs"]
mod tests;

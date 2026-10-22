use super::*;

// ====== mappings ======

#[test]
fn mappings() {
    assert_eq!(entry(0,0,0,8,0x200000),0x200003);
    for slot in 0..512 {
        let value=entry(2,slot,0xfed90000,8,0);
        if (1..=15).contains(&slot) && !slot.is_multiple_of(2) { assert_eq!(value,0x80000000fed90009); }
        else { assert_eq!(value,0); }
    }
    assert_eq!(entry(2,1,0xfed90001,8,0),u64::MAX);
    assert_eq!(entry(2,1,0xfed90000,4,0),u64::MAX);
    assert_eq!(entry(0,0,0,8,u64::MAX),u64::MAX);
    assert_eq!(entry(3,0,0,8,0),u64::MAX);
}

// ====== capability_fields ======

#[test]
fn capability_fields() {
    let cap=6|(1<<10)|(47<<16)|(0x22<<24);
    let ecap=1|(0xf<<8);
    assert_eq!(capabilities(0x10,cap,ecap,4096,0),2);
    assert_eq!(capabilities(0x10,cap,ecap,4096,1<<31),1);
    assert_eq!(capabilities(0x10,cap,ecap&!1,4096,0),1);
    assert_eq!(capabilities(0x10,cap&!(1<<10),ecap,4096,0),1);
    assert_eq!(capabilities(0x60,cap,ecap,4096,0),1);
    for (version,c,e,span) in [(0,cap,ecap,4096),(0x100,cap,ecap,4096),(0x10,0,ecap,4096),
        (0x10,u64::MAX,ecap,4096),(0x10,cap,u64::MAX,4096),(0x10,cap,ecap,4095),
        (0x10,cap|(1023<<24),ecap,4096),(0x10,cap,ecap|(1023<<8),4096)] {
        assert_eq!(capabilities(version,c,e,span,0),0);
    }
}

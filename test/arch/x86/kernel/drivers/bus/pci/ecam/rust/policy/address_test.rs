use super::*;

// ====== addresses ======

#[test]
fn addresses() {
    for bus in 0..256 { for device in 0..32 { for function in 0..8 {
        assert_eq!(page(0xe0000000,bus,device,function),0xe0000000+(u64::from(bus)<<20)+(u64::from(device)<<15)+(u64::from(function)<<12));
    } } }
    assert_eq!(page(0xe0000000,256,0,0),0);
    assert_eq!(page(0xe0000000,0,32,0),0);
    assert_eq!(page(0xe0000000,0,0,8),0);
    assert_eq!(page(u64::MAX,0,0,0),0);
}

// ====== ranges ======

#[test]
fn ranges() {
    for first in 0..256 { for last in first..256 {
        assert_eq!(range_end(0xe0000000,first,last,32),0xe0000000+((u64::from(last)+1)<<20));
    } }
    for bits in 0..64 {
        let valid=(32..=52).contains(&bits);
        assert_eq!(range_end(0xe0000000,4,7,bits)!=0,valid);
    }
    assert_eq!(range_end(0xfff00000,0,0,32),1u64<<32);
    assert_eq!(range_end(0xfff00000,1,1,32),0);
    assert_eq!(range_end(0xe0000000,7,4,52),0);
    assert_eq!(range_end(0xe0001000,0,0,52),0);
}

// ====== whitelist ======

#[test]
fn whitelist() {
    for at in 0..8192 { assert_eq!(offset(at),u32::from([0,4,8,12,16,24].contains(&at))); }
    assert_eq!(offset(u32::MAX),0);
}

// ====== command_bits ======

#[test]
fn command_bits() {
    assert_eq!(command(0xffff_0000,6,6),6);
    assert_eq!(command(0x0010_0407,4,0),0x0403);
    assert_eq!(command(0x0000_0001,2,2),3);
    for mask in 0..64 { for value in 0..64 {
        let valid=mask!=0 && mask&!6==0 && value&!mask==0;
        let result=command(0xabcd_1234,mask,value);
        assert_eq!(result==u32::MAX,!valid);
        if valid { assert!(result<=0xffff && result&!mask==0x1234&!mask && result&mask==value); }
    } }
}

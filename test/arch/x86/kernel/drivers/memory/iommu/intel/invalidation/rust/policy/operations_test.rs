use super::*;

// ====== cache_geometry ======

#[test]
fn cache_geometry() {
    let flags=(1<<19)|(1<<26);
    assert_eq!(cache_line(flags,8<<8),64);
    assert_eq!(cache_line(1<<26,8<<8),0);
    assert_eq!(cache_line(1<<19,8<<8),0);
    for encoded in 0..256 {
        let n=encoded*8u32;
        assert_eq!(cache_line(flags,encoded<<8),if n>=8 && n.is_power_of_two() {n} else {0});
    }
}

// ====== descriptors ======

#[test]
fn descriptors() {
    for token in [1,2,128,u32::MAX] {
        assert_eq!(descriptor(0,0,token,0x2000,0),0x11);
        assert_eq!(descriptor(1,0,token,0x2000,3<<54),0xd2);
        assert_eq!(descriptor(1,0,token,0x2000,0),0x12);
        assert_eq!(descriptor(2,0,token,0x2000,0),0x65|(u64::from(token)<<32));
        assert_eq!(descriptor(2,1,token,0x2000,0),0x2000);
    }
    for status in [0,4095,4097,1u64<<52,u64::MAX] { assert_eq!(descriptor(2,1,1,status,0),u64::MAX); }
    assert_eq!(descriptor(2,1,0,4096,0),u64::MAX);
    assert_eq!(descriptor(3,0,1,4096,0),u64::MAX);
    assert_eq!(descriptor(0,2,1,4096,0),u64::MAX);
}

// ====== register_bounds ======

#[test]
fn register_bounds() {
    for offset in 0..8192 {
        for width in [0,1,2,4,8,16,u32::MAX] {
            let read=(width==4 && [0x1c,0x34].contains(&offset)) ||
                (width==8 && [0x20,0x28,0x80,0x88,0x90,0xf8].contains(&offset));
            assert_eq!(register_allowed(offset,width,0,0,0x2000,0xf0),read);
            for value in [0,1,0x2000,1<<26,1<<31,u64::MAX] {
                let allowed=(width==4 && offset==0x18 && (value==0 || value==1<<26)) ||
                    (width==8 && offset==0x88 && value<4096 && value&15==0) ||
                    (width==8 && offset==0x90 && (value==0 || value==0x2000));
                assert_eq!(register_allowed(offset,width,1,value,0x2000,0xf0),allowed);
            }
        }
    }
}

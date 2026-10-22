use super::*;

// ====== layouts ======

#[test]
fn layouts() {
    assert_eq!(next(0x1001000,4100,4100),0x2006000);
    for pages in [0,4101,u64::MAX] { assert_eq!(next(0x1001000,pages,pages),0); }
    assert_eq!(next(u64::MAX,1,1),0);
    assert_eq!(next(0x2fff000,1,1),0);
    assert_eq!(next(0x1001000,2,1),0);
    assert_eq!(next(0x1001001,1,1),0);
}

// ====== formats ======

#[test]
fn formats() {
    for selector in 0..256 {
        for slot in 0..512 {
            assert_eq!(word(0,slot,selector,0x2000,0,0,48),if slot==selector*2 {0x2001} else {0});
            assert_eq!(word(1,slot,selector,0x3000,0,0,48),if slot==selector*2 {0x3001} else if slot==selector*2+1 {0x102} else {0});
        }
    }
    for bits in 32..=52 {
        assert_eq!(word(5,511,0,0,(1u64<<bits)-4096,3,bits),(1u64<<bits)-4096|3);
        assert_eq!(word(5,0,0,0,1u64<<bits,3,bits),u64::MAX);
    }
    for access in [0,2,4,7,u32::MAX] { assert_eq!(word(5,0,0,0,4096,access,48),u64::MAX); }
    assert_eq!(word(5,0,0,0,0,0,48),0);
    assert_eq!(word(4,17,0,0x2000,0,0,48),u64::MAX);
    assert_eq!(word(6,0,0,0,0,0,48),u64::MAX);
    assert_eq!(word(2,512,0,0x2000,0,0,48),u64::MAX);
    assert_eq!(word(3,0,0,0x2001,0,0,48),u64::MAX);
}

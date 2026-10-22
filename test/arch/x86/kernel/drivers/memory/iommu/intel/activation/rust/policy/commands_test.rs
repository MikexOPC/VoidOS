use super::*;
const ROOT:u64=0x0123_4000;
const FRO:u32=0x220;

// ====== commands ======

#[test]
fn commands() {
    assert_eq!(command(QIE,SRTP,1),u64::from(QIE|SRTP));
    assert_eq!(command(QIE|(1<<30),SRTP,1),u64::from(QIE|SRTP)); // stale RTPS is a one-shot bit
    assert_eq!(command(QIE|(1<<30),TE,1),u64::from(QIE|TE));
    assert_eq!(command(QIE|(1<<30)|TE,TE,0),u64::from(QIE));
    for bit in 0..32 {
        let other=1u32<<bit;
        if other!=TE && other!=SRTP { assert_eq!(command(QIE,other,1),u64::MAX); }
    }
    assert_eq!(command(QIE,SRTP,0),u64::MAX);
    assert_eq!(command(QIE,TE,2),u64::MAX);
    assert_eq!(command(0,TE,1),u64::MAX); // QI must stay enabled across activation
    for status in [QIE|(1<<29),QIE|(1<<27),QIE|(1<<24)] { assert_eq!(command(status,TE,1),u64::from(QIE|TE)); }
}

// ====== reads ======

#[test]
fn reads() {
    for offset in [0x1c,0x34,0x38] { assert!(allowed(offset,4,0,0,ROOT,FRO,4)); assert!(!allowed(offset,8,0,0,ROOT,FRO,4)); }
    assert!(allowed(0x20,8,0,0,ROOT,FRO,4));
    for slot in 0..4u32 {
        assert!(allowed(FRO+slot*16,8,0,0,ROOT,FRO,4));
        assert!(allowed(FRO+slot*16+8,8,0,0,ROOT,FRO,4));
        assert!(!allowed(FRO+slot*16+4,4,0,0,ROOT,FRO,4));
    }
    assert!(!allowed(FRO+64,8,0,0,ROOT,FRO,4));
    assert!(!allowed(FRO-16,8,0,0,ROOT,FRO,4));
    for offset in [0,8,0x10,0x18,0x28,0x80,0x88,0x90,0x3c,0x40,0x44] {
        assert!(!allowed(offset,4,0,0,ROOT,FRO,4) || offset==0x1c);
        assert!(!allowed(offset,8,0,0,ROOT,FRO,4));
    }
}

// ====== writes ======

#[test]
fn writes() {
    for value in [QIE|SRTP,QIE|TE,QIE] { assert!(allowed(0x18,4,1,u64::from(value),ROOT,FRO,4)); }
    for value in [0,TE,SRTP,QIE|SRTP|TE,QIE|(1<<25),QIE|(1<<24),QIE|(1<<27)] { assert!(!allowed(0x18,4,1,u64::from(value),ROOT,FRO,4)); }
    assert!(allowed(0x20,8,1,ROOT,ROOT,FRO,4));
    assert!(allowed(0x20,8,1,0,ROOT,FRO,4));
    for value in [ROOT|0x400,ROOT|0xc00,ROOT+4096,1] { assert!(!allowed(0x20,8,1,value,ROOT,FRO,4)); }
    assert!(allowed(0x34,4,1,1,ROOT,FRO,4));
    for value in [0,2,0x10,0x20,0x40,0x71] { assert!(!allowed(0x34,4,1,value,ROOT,FRO,4)); }
    assert!(allowed(FRO+8,8,1,1u64<<63,ROOT,FRO,4));
    assert!(!allowed(FRO,8,1,0,ROOT,FRO,4));
    assert!(!allowed(FRO+8,8,1,(1u64<<63)|1,ROOT,FRO,4));
    for offset in [0x38,0x88,0x90,0x1c,0x28] { assert!(!allowed(offset,4,1,0,ROOT,FRO,4) && !allowed(offset,8,1,0,ROOT,FRO,4)); }
}

// ====== geometry ======

#[test]
fn geometry() {
    assert!(!allowed(0x1c,4,0,0,0,FRO,4));
    assert!(!allowed(0x1c,4,0,0,ROOT|1,FRO,4));
    assert!(!allowed(0x1c,4,0,0,1u64<<52,FRO,4));
    assert!(!allowed(0x1c,4,0,0,ROOT,FRO,0));
    assert!(!allowed(0x1c,4,0,0,ROOT,FRO,257));
    assert!(!allowed(0x1c,4,0,0,ROOT,0xf0,4));
    assert!(!allowed(0x1c,4,0,0,ROOT,FRO+8,4));
    assert!(!allowed(0x1c,4,0,0,ROOT,4096-48,4));
    assert!(allowed(0x1c,4,0,0,ROOT,4096-64,4));
    assert!(!allowed(4093,4,0,0,ROOT,FRO,4));
    assert!(!allowed(0x1c,4,2,0,ROOT,FRO,4));
    assert!(!allowed(0x1c,2,0,0,ROOT,FRO,4));
}

// ====== records ======

#[test]
fn records() {
    assert_eq!(decode(0),0);
    assert_eq!(decode((1u64<<62)|0x0008),0);
    let read=(1u64<<63)|(1u64<<62)|(0x06u64<<32)|0x0008;
    assert_eq!(decode(read),(1u64<<63)|(1<<24)|(0x06<<16)|0x0008);
    let write=(1u64<<63)|(0x02u64<<32)|0x00f8;
    assert_eq!(decode(write),(1u64<<63)|(0x02<<16)|0x00f8);
    for bit in 16..28 { assert_eq!(decode((1u64<<63)|(1u64<<bit)),u64::MAX); }
}

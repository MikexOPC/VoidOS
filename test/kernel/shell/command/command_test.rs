use super::*;

// ====== builtins_and_arguments ======

#[test]
fn builtins_and_arguments() {
    for (word,len,kind) in [(0,0,0),(0x706c6568,4,1),(0x746e697270,5,2),
        (0x737574617473,6,3),(0x74697865,4,4),(0x706c6548,4,5)] {
        assert_eq!(classify(word,len,0),kind);
        if matches!(kind,1 | 3 | 4) { assert_eq!(classify(word,len,1),6); }
    }
    assert_eq!(classify(0x746e697270,5,1),2);
    assert_eq!(classify(0x6c66,2,0),10);
    assert_eq!(classify(0x6c66,2,1),10);
    assert_eq!(classify(0x7072,2,1),11);
    assert_eq!(classify(0x7072,2,0),6);
    assert_eq!(classify(0x6f686365,4,0),5);
    assert_eq!(classify(0x736c,2,0),5);
    assert_eq!(classify(0x746163,3,1),5);
    assert_eq!(classify(0x6574697277,5,1),12);
    assert_eq!(classify(0x6574697277,5,0),6);
    assert_eq!(classify(0x6d72,2,1),13);
    assert_eq!(classify(0x6d72,2,0),6);
    assert_eq!(classify(0x636e7973,4,0),14);
    assert_eq!(classify(0x636e7973,4,1),6);
    assert_eq!(classify(0,0,1),u32::MAX);
    assert_eq!(classify(1,0,0),u32::MAX);
    assert_eq!(classify(0x100706c6568,4,0),u32::MAX);
}

// ====== bounds_and_mutated_tokens ======

#[test]
fn bounds_and_mutated_tokens() {
    for length in 0..=257 {
        for arguments in 0..=2 {
            for word in [0,u64::MAX,0x706c6568,0x737574617473] {
                let result = classify(word,length,arguments);
                assert!(result <= 14 || result == u32::MAX);
                if length > 256 || arguments > 1 { assert_eq!(result,u32::MAX); }
            }
        }
    }
    for bit in 0..64 { assert_ne!(classify(0x74697865 ^ (1 << bit),4,0),4); }
}

// ====== ap_command_and_count ======

#[test]
fn ap_command_and_count() {
    assert_eq!(classify(0x7061,2,1),7);
    assert_eq!(classify(0x7061,2,0),6);
    assert_eq!(classify(0x7041,2,1),5);
    assert_eq!(classify(0x656764697262,6,1),8);
    assert_eq!(classify(0x656764697262,6,0),6);
    for (text, value) in [("1",1),("1000",1000),("1000000",1_000_000),("0042",42)] {
        let word = text.bytes().enumerate().fold(0u64,|acc,(i,b)| acc | (u64::from(b) << (8 * i)));
        assert_eq!(count(word,text.len() as u32),value);
    }
    for text in ["0","12a","-1"," 1"] {
        let word = text.bytes().enumerate().fold(0u64,|acc,(i,b)| acc | (u64::from(b) << (8 * i)));
        assert_eq!(count(word,text.len() as u32),u64::MAX);
    }
    assert_eq!(count(0,0),u64::MAX);
    assert_eq!(count(0x3131,1),u64::MAX);
    assert_eq!(count(0x3131313131313131,8),11_111_111);
}

// ====== extended_ap_count ======

#[test]
fn extended_ap_count() {
    for (text, expected) in [("1000001",1_000_001),("100000000",100_000_000),
        ("100000001",u64::MAX),("999999999",u64::MAX),("1000000000",u64::MAX),
        ("10000000x",u64::MAX)] {
        let word = text.bytes().enumerate().fold(0u128,|value,(index,byte)|
            value | (u128::from(byte) << (index * 8)));
        assert_eq!(number(word as u64,(word >> 64) as u64,text.len() as u32),expected);
    }
    assert_eq!(number(0x3030303030303031,0x30,9),100_000_000);
    assert_eq!(number(0x3030303030303031,0x130,9),u64::MAX);
    assert_eq!(number(0x31,1,1),u64::MAX);
    assert_eq!(number(0,0,0),u64::MAX);
}

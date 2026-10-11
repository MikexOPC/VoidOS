use super::*;

// ====== builtins_and_arguments ======

#[test]
fn builtins_and_arguments() {
    for (word,len,kind) in [(0,0,0),(0x706c6568,4,1),(0x6f686365,4,2),
        (0x737574617473,6,3),(0x74697865,4,4),(0x706c6548,4,5)] {
        assert_eq!(classify(word,len,0),kind);
        if matches!(kind,1 | 3 | 4) { assert_eq!(classify(word,len,1),6); }
    }
    assert_eq!(classify(0x6f686365,4,1),2);
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
                assert!(result <= 6 || result == u32::MAX);
                if length > 256 || arguments > 1 { assert_eq!(result,u32::MAX); }
            }
        }
    }
    for bit in 0..64 { assert_ne!(classify(0x74697865 ^ (1 << bit),4,0),4); }
}

use super::{bar, region};

// ====== bar_limits ======

#[test]
fn bar_limits() {
    assert_eq!(bar(0x80000000,0,4096,48),0x80000000);
    assert_eq!(bar(4,1,4096,48),1u64 << 32);
    assert_eq!(bar(4,0xffff,4096,48),0xffff00000000);
    for bits in 0..=64 {
        assert_eq!(bar(0x80000000,0,4096,bits) != u64::MAX,(32..=52).contains(&bits));
    }
    for low in 0..16 {
        assert_eq!(bar(0x80000000|low,0,4096,48) != u64::MAX,matches!(low&7,0|4));
    }
    for size in 0..16385 {
        assert_eq!(bar(0x80000000,0,size,48) != u64::MAX,size >= 16 && size.is_power_of_two());
    }
    assert_eq!(bar(0,0,4096,48),u64::MAX);
    assert_eq!(bar(0xfffff004,0xfffff,4096,52),0xffffffffff000);
    assert_eq!(bar(0xfffff004,0x100000,4096,52),u64::MAX);
    assert_eq!(bar(0x80000000,1,4096,48),u64::MAX);
}

// ====== region_boundaries ======

#[test]
fn region_boundaries() {
    let base = 0x80000000;
    for kind in 1..=4 {
        let minimum = [56,2,1,16][kind as usize-1];
        for length in 0..=80 {
            assert_eq!(region(kind,0,length,0,base,4096) != u64::MAX,length >= minimum);
        }
        assert_eq!(region(kind,4096,minimum,0,base,4096),u64::MAX);
        assert_eq!(region(kind,0,u32::MAX,0,base,4096),u64::MAX);
    }
    for multiplier in 0u32..=4096 {
        assert_eq!(region(2,0,4096,multiplier,base,4096) != u64::MAX,
            multiplier == 0 || (multiplier >= 2 && multiplier.is_power_of_two()));
    }
    assert_eq!(region(1,4040,56,0,base,4096),base+4040);
    assert_eq!(region(1,4041,56,0,base,4096),u64::MAX);
    assert_eq!(region(3,4095,1,0,base,4096),base+4095);
    assert_eq!(region(4,4080,16,0,base,4096),base+4080);
    assert_eq!(region(4,0,16,0,u64::MAX-1,4096),u64::MAX);
    assert_eq!(region(4,0,16,1,base,4096),u64::MAX);
    assert_eq!(region(0,0,16,0,base,4096),u64::MAX);
}

use super::{Ranges, build};
use std::collections::BTreeSet;

// ====== limits_and_union ======

#[test]
fn limits_and_union() {
    assert_eq!(std::mem::size_of::<Ranges>(),64);
    assert_eq!(std::mem::size_of::<super::Plan>(),168);
    for bits in 0..=64 {
        assert_eq!(build(Ranges::default(),bits).valid,(32..=52).contains(&bits) as u32);
    }
    for offset in 0..4096 {
        let input=Ranges {address:[0x100000000+offset,0x100003000,0x100000800,0x100002000],length:[56,4096,1,16]};
        let plan=build(input,48);
        let mut expected=BTreeSet::new();
        for (&address,&length) in input.address.iter().zip(input.length.iter()) {
            for page in address/4096..=(address+length-1)/4096 { expected.insert(page*4096); }
        }
        let expected:Vec<_>=expected.into_iter().collect();
        assert_eq!(plan.valid,1);
        assert_eq!(plan.physical[..plan.count as usize],expected);
        for (&address,&offset) in input.address.iter().zip(plan.offset.iter()) {
            assert_eq!(plan.physical[(offset/4096) as usize]+offset%4096,address);
        }
        assert!(plan.physical[plan.count as usize..].iter().all(|v| *v==0));
    }
}

// ====== rejected_and_boundary ======

#[test]
fn rejected_and_boundary() {
    let input=Ranges {address:[0x1000;4],length:[16*4096;4]};
    assert_eq!(build(input,32).count,16);
    assert_eq!(build(Ranges{length:[16*4096+1;4],..input},32).valid,0);
    assert_eq!(build(Ranges{address:[u64::MAX-4;4],length:[8;4]},52).valid,0);
    assert_eq!(build(Ranges{address:[1u64<<32;4],length:[1;4]},32).valid,0);
    assert_eq!(build(Ranges{address:[(1u64<<52)-1;4],length:[1;4]},52).count,1);
    assert_eq!(build(Ranges{length:[0;4],..input},48).valid,0);
    assert_eq!(build(Ranges{address:[0;4],..input},48).valid,0);
    let extra=Ranges{address:[0x1000,0x2000,0x100000,0x200000],length:[14*4096,1,1,1]};
    assert_eq!(build(extra,48).count,16);
    assert_eq!(build(Ranges{length:[15*4096,1,1,1],..extra},48).valid,0);
}

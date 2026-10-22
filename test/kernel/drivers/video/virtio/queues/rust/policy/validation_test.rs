use super::*;

// ====== sizes ======

#[test]
fn sizes() {
    for value in 0..=65536 {
        assert_eq!(size(value),u32::from((8..=32768).contains(&value) && value.is_power_of_two()));
    }
}

// ====== counters ======

#[test]
fn counters() {
    for old in 0..65536 {
        let next=(old+1)&65535;
        assert_eq!(completion(old,next,0,408),1);
        for bad in [old,(old+2)&65535,65536] { assert_eq!(completion(old,bad,0,408),0); }
        assert_eq!(completion(old,next,1,408),0);
        for bytes in [0,24,407,409,4096,u32::MAX] { assert_eq!(completion(old,next,0,bytes),0); }
    }
}

// ====== registers ======

#[test]
fn registers() {
    assert_eq!(register(0x20,4,1),1);
    assert_eq!(register(0x12,2,1),0);
    assert_eq!(register(0x1e,2,1),0);
    for width in [0,3,8,u32::MAX] { assert_eq!(register(0x20,width,1),0); }
    for offset in [0,4,8,12,0x15,0x38,u32::MAX] { assert_eq!(register(offset,2,0),0); }
    assert_eq!(register(0x20,4,2),0);
}

// ====== displays ======

#[test]
fn displays() {
    assert_eq!(display(0,0,1024,768,1,0),1);
    assert_eq!(display(0,0,0,0,0,0),1);
    assert_eq!(display(u32::MAX,0,1024,768,1,0),0);
    assert_eq!(display(0,u32::MAX,1024,768,1,0),0);
    for size in [0,16385,u32::MAX] { assert_eq!(display(0,0,size,1,1,0),0); }
    assert_eq!(display(0,0,1,1,2,0),0);
    assert_eq!(display(0,0,1,1,1,1),0);
}

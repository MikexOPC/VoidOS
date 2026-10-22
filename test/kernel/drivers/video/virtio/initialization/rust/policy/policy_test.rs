use super::{features,register_valid};

// ====== minimal_features ======

#[test]
fn minimal_features() {
    for bit in 0..64 {
        let value=1u64<<bit;
        assert_eq!(features(value),if bit==32 {value} else {0});
        assert_eq!(features(value|(1u64<<32)),1u64<<32);
    }
    assert_eq!(features(u64::MAX),1u64<<32);
}

// ====== exact_register_widths ======

#[test]
fn exact_register_widths() {
    for offset in 0..128 {
        for width in 0..9 {
            for write in 0..3 {
                let expected=write<=1 && ((width==4 && [0,8,12].contains(&offset)) ||
                    (width==4 && offset==4 && write==0) || (offset==20 && width==1));
                assert_eq!(register_valid(offset,width,write),u32::from(expected));
            }
        }
    }
    assert_eq!(register_valid(u32::MAX,4,1),0);
}

use super::*;

// ====== unicode_exhaustive_scalars_and_boundaries ======

#[test]
fn unicode_exhaustive_scalars_and_boundaries() {
    for scalar in 0..=0x110000 {
        let encoded = encode(scalar);
        let Some(character) = char::from_u32(scalar) else { assert_eq!(encoded,0); continue; };
        let length = (encoded >> 32) as usize;
        assert!((1..=4).contains(&length));
        let bytes = (encoded as u32).to_le_bytes();
        let text = std::str::from_utf8(&bytes[..length]).unwrap();
        assert_eq!(text.chars().next(),Some(character));
        assert_eq!(text.chars().count(),1);
    }
    assert_eq!(encode(u32::MAX),0);
    assert_eq!(encode(0xe8), (2u64 << 32) | 0xa8c3);
    assert_eq!(encode(0x20ac), (3u64 << 32) | 0xac82e2);
    assert_eq!(encode(0x1f600), (4u64 << 32) | 0x80989ff0);
}

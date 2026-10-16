use super::{x86_fb_font_scalar as scalar, x86_fb_rect_valid as rect, x86_fb_utf8_scalar as decode};

// ====== font_height_contract ======

#[test]
fn font_height_contract() {
    for height in 0..=256 { assert_eq!(super::x86_fb_font_height(height), u32::from(height == 8 || height == 16 || height == 20)); }
    assert_eq!(super::x86_fb_font_height(u32::MAX), 0);
}

// ====== pair ======

fn pair(x: u32, y: u32) -> u64 { u64::from(x) | (u64::from(y) << 32) }

// ====== rectangle_bounds_and_work ======

#[test]
fn rectangle_bounds_and_work() {
    assert_eq!(rect(pair(1920, 1080), pair(1919, 1079), pair(1, 1)), 1);
    assert_eq!(rect(pair(64, 64), 0, pair(64, 64)), 1);
    assert_eq!(rect(pair(65, 64), 0, pair(65, 64)), 0);
    for value in [0, u32::MAX] {
        assert_eq!(rect(pair(value, 64), 0, pair(1, 1)), 0);
        assert_eq!(rect(pair(64, value), 0, pair(1, 1)), 0);
        assert_eq!(rect(pair(64, 64), 0, pair(value, 1)), 0);
        assert_eq!(rect(pair(64, 64), 0, pair(1, value)), 0);
    }
    for origin in [pair(64, 0), pair(0, 64), u64::MAX] {
        assert_eq!(rect(pair(64, 64), origin, pair(1, 1)), 0);
    }
    assert_eq!(rect(pair(64, 64), pair(63, 63), pair(2, 1)), 0);
    assert_eq!(rect(pair(64, 64), pair(63, 63), pair(1, 2)), 0);
}

// ====== all_unicode_scalar_roundtrips ======

#[test]
fn all_unicode_scalar_roundtrips() {
    for code in 0..=0x10ffff {
        let Some(ch) = char::from_u32(code) else { assert_eq!(scalar(code), 0); continue; };
        let mut bytes = [0u8; 4];
        let length = ch.encode_utf8(&mut bytes).len() as u32;
        let expected = if code < 0x20 || (0x7f..=0x9f).contains(&code) { u64::MAX }
            else { u64::from(code) | (u64::from(length) << 32) };
        assert_eq!(decode(u32::from_le_bytes(bytes), length), expected);
    }
    assert_eq!(scalar(0x110000), 0);
    assert_eq!(scalar(u32::MAX), 0);
}

// ====== malformed_and_truncated_utf8 ======

#[test]
fn malformed_and_truncated_utf8() {
    for bytes in [[0xc0, 0xaf, 0, 0], [0xc1, 0xbf, 0, 0], [0xe0, 0x9f, 0x80, 0],
        [0xed, 0xa0, 0x80, 0], [0xf0, 0x8f, 0xbf, 0xbf], [0xf4, 0x90, 0x80, 0x80],
        [0xf5, 0x80, 0x80, 0x80], [0x80, 0, 0, 0], [0xc2, b'A', 0, 0],
        [0xe1, 0x80, b'A', 0], [0xf1, 0x80, 0x80, b'A']] {
        assert_eq!(decode(u32::from_le_bytes(bytes), 4), u64::MAX);
    }
    for (bytes, length) in [([0xc3, 0xa0, 0, 0], 2), ([0xe2, 0x82, 0xac, 0], 3),
        ([0xf0, 0x9f, 0x98, 0x80], 4)] {
        for short in 0..length { assert_eq!(decode(u32::from_le_bytes(bytes), short), u64::MAX); }
    }
    assert_eq!(decode(u32::from(b'A'), 257), u64::MAX);
}

// ====== italian_and_byte_capacity ======

#[test]
fn italian_and_byte_capacity() {
    for ch in ['à', 'è', 'é', 'ì', 'ò', 'ù', 'À', 'È', 'É', 'Ì', 'Ò', 'Ù', '€'] {
        let mut bytes = [0u8; 4];
        let count = ch.encode_utf8(&mut bytes).len() as u32;
        assert_eq!(decode(u32::from_le_bytes(bytes), 256), u64::from(u32::from(ch)) | (u64::from(count) << 32));
    }
    assert_eq!(decode(0xffffffff, 4), u64::MAX);
    assert_eq!(decode(0xffffff41, 1), (1 << 32) | 65);
}

#[path = "../../../../../../kernel/console/video/rust/text/utf8.rs"]
mod utf8;

// ====== exhaustive_scalars ======

#[test]
fn exhaustive_scalars() {
    for value in 0..=0x10ffff {
        let Some(scalar) = char::from_u32(value) else { continue; };
        let mut bytes = [0_u8; 4];
        let size = scalar.encode_utf8(&mut bytes).len() as u32;
        let packed = u32::from_le_bytes(bytes);
        for controls in 0..=1 {
            let valid = !scalar.is_control() || controls == 1 && matches!(scalar, '\n' | '\r');
            let expected = if valid { u64::from(value) | (u64::from(size) << 32) } else { u64::MAX };
            assert_eq!(utf8::kernel_console_utf8(packed, size, controls), expected);
            for available in 1..size {
                assert_eq!(utf8::kernel_console_utf8(packed, available, controls), u64::MAX);
            }
        }
    }
}

// ====== invalid_and_continuations ======

#[test]
fn invalid_and_continuations() {
    for first in 0..=255_u32 {
        for second in 0..=255_u32 {
            let packed = first | (second << 8);
            let bytes = [first as u8, second as u8];
            let result = utf8::kernel_console_utf8(packed, 2, 0);
            if first < 128 {
                assert_eq!(result != u64::MAX, !char::from(first as u8).is_control());
            } else {
                let valid = core::str::from_utf8(&bytes).ok()
                    .and_then(|text| text.chars().next()).is_some_and(|scalar| !scalar.is_control());
                assert_eq!(result != u64::MAX, valid);
            }
        }
    }
    for bytes in [[0xc0, 0x80, 0, 0], [0xe0, 0x80, 0x80, 0], [0xed, 0xa0, 0x80, 0],
        [0xf0, 0x80, 0x80, 0x80], [0xf4, 0x90, 0x80, 0x80], [0xf5, 0x80, 0x80, 0x80]] {
        assert_eq!(utf8::kernel_console_utf8(u32::from_le_bytes(bytes), 4, 1), u64::MAX);
    }
    assert_eq!(utf8::kernel_console_utf8(65, 0, 0), u64::MAX);
    assert_eq!(utf8::kernel_console_utf8(65, 5, 0), u64::MAX);
    assert_eq!(utf8::kernel_console_utf8(65, 1, 2), u64::MAX);
}

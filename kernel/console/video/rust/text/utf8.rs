#![cfg_attr(not(test), no_std)]
#![deny(unsafe_code)]

// ====== kernel_console_utf8 ======

#[allow(unsafe_code)]
#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_utf8(packed: u32, available: u32, controls: u32) -> u64 {
    if available == 0 || available > 4 || controls > 1 {
        return u64::MAX;
    }
    let bytes = packed.to_le_bytes();
    let (count, mut scalar, minimum) = match bytes[0] {
        0..=0x7f => (1, u32::from(bytes[0]), 0),
        0xc2..=0xdf => (2, u32::from(bytes[0] & 31), 0x80),
        0xe0..=0xef => (3, u32::from(bytes[0] & 15), 0x800),
        0xf0..=0xf4 => (4, u32::from(bytes[0] & 7), 0x10000),
        _ => return u64::MAX,
    };
    if count > available as usize {
        return u64::MAX;
    }
    for &byte in bytes.iter().take(count).skip(1) {
        if byte & 0xc0 != 0x80 { return u64::MAX; }
        scalar = (scalar << 6) | u32::from(byte & 63);
    }
    if scalar < minimum || scalar > 0x10ffff || (0xd800..=0xdfff).contains(&scalar) {
        return u64::MAX;
    }
    if (scalar < 32 || (127..=159).contains(&scalar)) &&
        !(controls == 1 && matches!(scalar, 10 | 13)) {
        return u64::MAX;
    }
    u64::from(scalar) | ((count as u64) << 32)
}

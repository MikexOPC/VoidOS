#![cfg_attr(not(test), no_std)]

// ====== x86_fb_rect_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_fb_rect_valid(surface: u64, origin: u64, extent: u64) -> u32 {
    let width = surface as u32;
    let height = (surface >> 32) as u32;
    let x = origin as u32;
    let y = (origin >> 32) as u32;
    let w = extent as u32;
    let h = (extent >> 32) as u32;
    u32::from((1..=1920).contains(&width) && (1..=1080).contains(&height) &&
        x < width && y < height && w != 0 && h != 0 && w <= width - x &&
        h <= height - y && u64::from(w) * u64::from(h) <= 4096)
}

// ====== x86_fb_font_scalar ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_fb_font_scalar(scalar: u32) -> u32 {
    u32::from(char::from_u32(scalar).is_some() && scalar >= 0x20 &&
        !(0x7f..=0x9f).contains(&scalar))
}

// ====== x86_fb_utf8_scalar ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_fb_utf8_scalar(packed: u32, remaining: u32) -> u64 {
    let bytes = packed.to_le_bytes();
    let length = match bytes[0] {
        0..=0x7f => 1,
        0xc2..=0xdf => 2,
        0xe0..=0xef => 3,
        0xf0..=0xf4 => 4,
        _ => return u64::MAX,
    };
    if remaining < length || remaining > 256 { return u64::MAX; }
    if length > 1 && (!(0x80..=0xbf).contains(&bytes[1]) ||
        (bytes[0] == 0xe0 && bytes[1] < 0xa0) || (bytes[0] == 0xed && bytes[1] > 0x9f) ||
        (bytes[0] == 0xf0 && bytes[1] < 0x90) || (bytes[0] == 0xf4 && bytes[1] > 0x8f)) {
        return u64::MAX;
    }
    let mut scalar = u32::from(bytes[0] & match length { 1 => 0x7f, 2 => 0x1f, 3 => 0x0f, _ => 7 });
    for byte in bytes.iter().take(length as usize).skip(1) {
        if !(0x80..=0xbf).contains(byte) { return u64::MAX; }
        scalar = (scalar << 6) | u32::from(byte & 0x3f);
    }
    if x86_fb_font_scalar(scalar) == 0 { return u64::MAX; }
    u64::from(scalar) | (u64::from(length) << 32)
}

#[cfg(test)]
#[path = "../../test/rust/drawing/validation_test.rs"]
mod tests;

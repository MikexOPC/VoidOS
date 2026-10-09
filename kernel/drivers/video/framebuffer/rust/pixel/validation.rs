#![forbid(unsafe_code)]

#[path = "../mode/validation.rs"]
pub mod mode;

// ====== pixel_offset ======

pub fn pixel_offset(width: u32, height: u32, stride: u32, format: u32,
    buffer_bytes: u64, x: u32, y: u32) -> u64 {
    let used = mode::fb_mode_bytes(width, height, stride, format);
    if used == 0 || buffer_bytes < used || x >= width || y >= height {
        return u64::MAX;
    }
    let Some(offset) = u64::from(y).checked_mul(u64::from(stride))
        .and_then(|row| row.checked_add(u64::from(x)))
        .and_then(|pixel| pixel.checked_mul(4)) else { return u64::MAX; };
    if offset > buffer_bytes - 4 { return u64::MAX; }
    offset
}

// ====== color_word ======

pub fn color_word(format: u32, red: u8, green: u8, blue: u8) -> u32 {
    match format {
        0 => u32::from_le_bytes([red, green, blue, 0]),
        1 => u32::from_le_bytes([blue, green, red, 0]),
        _ => u32::MAX,
    }
}

#[cfg(test)]
#[path = "../../test/pixel/validation_test.rs"]
mod tests;

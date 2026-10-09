#![forbid(unsafe_code)]

// ====== fb_mode_bytes ======

pub fn fb_mode_bytes(width: u32, height: u32, stride: u32, format: u32) -> u64 {
    // Operational v0 budget, not a claim about all GOP modes/devices.
    if width == 0 || width > 1920 || height == 0 || height > 1080 ||
        stride < width || stride > 4096 || format > 1 { return 0; }
    u64::from(stride) * u64::from(height) * 4
}

// ====== fb_mode_score ======

pub fn fb_mode_score(width: u32, height: u32, stride: u32, format: u32) -> u32 {
    if fb_mode_bytes(width, height, stride, format) == 0 { return u32::MAX; }
    width.abs_diff(1024) + height.abs_diff(768)
}

// ====== fb_descriptor_bytes ======

pub fn fb_descriptor_bytes(base: u64, capacity: u64, width: u32,
    height: u32, stride: u32, format: u32, physical_bits: u32) -> u64 {
    let used = fb_mode_bytes(width, height, stride, format);
    if used == 0 || base == 0 || base & 3 != 0 || capacity < used ||
        capacity > 32 * 1024 * 1024 || !(32..=52).contains(&physical_bits) { return 0; }
    let limit = 1u64 << physical_bits;
    if base >= limit || capacity > limit - base { return 0; }
    used
}

#[cfg(test)]
#[path = "../../test/mode/validation_test.rs"]
mod tests;

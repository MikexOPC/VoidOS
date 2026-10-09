#![no_std]
#[path = "../pixel/validation.rs"]
pub mod pixel;
pub use pixel::mode as validation;
#[path = "../mapping/geometry.rs"]
pub mod mapping;

// ====== fb_mode_bytes ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_mode_bytes(w: u32, h: u32, stride: u32, format: u32) -> u64 {
    validation::fb_mode_bytes(w,h,stride,format)
}

// ====== fb_mode_score ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_mode_score(w: u32, h: u32, stride: u32, format: u32) -> u32 {
    validation::fb_mode_score(w,h,stride,format)
}

// ====== fb_descriptor_bytes ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_descriptor_bytes(base: u64, capacity: u64, w: u32,
    h: u32, stride: u32, format: u32, bits: u32) -> u64 {
    validation::fb_descriptor_bytes(base,capacity,w,h,stride,format,bits)
}

// ====== fb_pixel_offset ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_pixel_offset(w: u32, h: u32, stride: u32, format: u32,
    bytes: u64, x: u32, y: u32) -> u64 {
    pixel::pixel_offset(w, h, stride, format, bytes, x, y)
}

// ====== fb_color_word ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_color_word(format: u32, red: u8, green: u8, blue: u8) -> u32 {
    pixel::color_word(format, red, green, blue)
}

// ====== fb_mapping_pages ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_mapping_pages(base: u64, capacity: u64, used: u64, bits: u32) -> u64 {
    mapping::mapping_pages(base, capacity, used, bits)
}

// ====== fb_mapping_tables ======

#[unsafe(no_mangle)]
pub extern "C" fn fb_mapping_tables(pages: u64) -> u64 {
    mapping::mapping_tables(pages)
}

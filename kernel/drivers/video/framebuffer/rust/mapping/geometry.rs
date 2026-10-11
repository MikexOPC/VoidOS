#![forbid(unsafe_code)]

// ====== mapping_pages ======

pub fn mapping_pages(base: u64, capacity: u64, used: u64, bits: u32) -> u64 {
    if !(32..=52).contains(&bits) || base == 0 || base & 3 != 0 ||
        used == 0 || used > 4096 * 1080 * 4 || used & 3 != 0 ||
        capacity < used || capacity > 32 * 1024 * 1024 {
        return 0;
    }
    let limit = 1u64 << bits;
    if base >= limit || capacity > limit - base { return 0; }
    let Some(span) = (base & 4095).checked_add(used) else { return 0; };
    let Some(rounded) = span.checked_add(4095) else { return 0; };
    rounded / 4096
}

// ====== mapping_tables ======

pub fn mapping_tables(pages: u64) -> u64 {
    if pages == 0 || pages > 4321 { return 0; }
    (pages + 2).div_ceil(512) + 2
}

#[cfg(test)]
#[path = "../../../../../../test/kernel/drivers/video/framebuffer/mapping/geometry_test.rs"]
mod tests;

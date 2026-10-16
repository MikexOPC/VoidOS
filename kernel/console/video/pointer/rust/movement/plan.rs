#![forbid(unsafe_code)]

// ====== position ======

pub fn position(width: u32, height: u32, x: u32, y: u32, dx: i32, dy: i32) -> u64 {
    if !(24..=1920).contains(&width) || !(24..=1072).contains(&height)
        || x > width - 24 || y > height - 24
        || !(-256..=255).contains(&dx) || !(-256..=255).contains(&dy) {
        return u64::MAX;
    }
    let next_x = (i64::from(x) + i64::from(dx)).clamp(0, i64::from(width - 24)) as u32;
    let next_y = (i64::from(y) - i64::from(dy)).clamp(0, i64::from(height - 24)) as u32;
    u64::from(next_x) | (u64::from(next_y) << 32)
}

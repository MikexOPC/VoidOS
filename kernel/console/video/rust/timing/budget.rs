#![forbid(unsafe_code)]
pub const GOAL: u32 = 35_796;
pub const LIMIT: u32 = 357_955;

// ====== sample ======

pub fn sample(start: u32, end: u32, mask: u32) -> u64 {
    if !matches!(mask, 0x00ff_ffff | u32::MAX) || (start | end) & !mask != 0 {
        return u64::MAX;
    }
    let ticks = end.wrapping_sub(start) & mask;
    let status = if ticks == 0 { 2 } else if ticks > LIMIT { 3 } else { 1 };
    ((status as u64) << 32) | u64::from(ticks)
}

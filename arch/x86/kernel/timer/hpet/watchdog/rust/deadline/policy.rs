#![cfg_attr(not(test), no_std)]

// ====== x86_watchdog_ticks ======

// HPET 1.0a section 2.3.4: period in fs. Supported v0 deadline 1..1000ms;
// restrict 32-bit one-shot distance to less than half the counter range.
#[unsafe(no_mangle)]
pub extern "C" fn x86_watchdog_ticks(period: u32, milliseconds: u32) -> u32 {
    if !(1..=100_000_000).contains(&period) || !(1..=1000).contains(&milliseconds) { return 0; }
    let ticks = (u64::from(milliseconds) * 1_000_000_000_000).div_ceil(u64::from(period));
    if ticks < 2 || ticks > 0x7fff_ffff { return 0; }
    ticks as u32
}

// ====== x86_watchdog_ahead ======

// Reject an arm whose programming already consumed half its time margin.
#[unsafe(no_mangle)]
pub extern "C" fn x86_watchdog_ahead(start: u32, now: u32, ticks: u32) -> u32 {
    u32::from((2..=0x7fff_ffff).contains(&ticks) && now.wrapping_sub(start) < ticks / 2)
}

// ====== x86_watchdog_classify ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_watchdog_classify(count: u32, error: u32, goal: u32, stop: u32) -> u32 {
    if goal == 0 || error != 0 || stop > 1 || count > goal { return 3; }
    if stop == 1 { return 4; }
    if count == goal { 1 } else { 2 }
}

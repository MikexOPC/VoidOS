#![no_std]
#[path = "../timing/budget.rs"]
mod budget;

// ====== kernel_console_pm_sample ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_pm_sample(start: u32, end: u32, mask: u32) -> u64 {
    budget::sample(start, end, mask)
}

// ====== kernel_console_pm_limit ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_pm_limit() -> u32 {
    budget::LIMIT
}

// ====== kernel_console_pm_goal ======

#[unsafe(no_mangle)]
pub extern "C" fn kernel_console_pm_goal() -> u32 {
    budget::GOAL
}

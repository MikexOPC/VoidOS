#![cfg_attr(not(test), no_std)]

// ====== x86_watchdog_recovery_policy ======

// Snapshot flags: self/closed/armed/error/acquired/route/ready/token, bits0..7.
// Pure classification: C checks canonical ownership, CPU, mapping and MMIO.
// A retry candidate is not proof of hardware quiescence or recovered service.
#[unsafe(no_mangle)]
pub extern "C" fn x86_watchdog_recovery_policy(
    flags: u32, failure: u32, cleanup: u32, attempts: u32,
) -> u32 {
    if flags > 255 || failure > 23 || cleanup > 23 || attempts > 3 { return 5; }
    if flags & 1 == 0 {
        if flags != 0 || cleanup != 0 || attempts != 0 { return 5; }
        return if failure == 0 { 0 } else { 1 };
    }
    if flags & 2 != 0 { return if flags == 3 { 6 } else { 5 }; }
    if flags & 12 != 0 || failure == 13 || failure == 14 { return 5; }
    if flags & 128 != 0 && flags & 112 != 112 { return 5; }
    if cleanup != 0 {
        if !(18..=22).contains(&cleanup) { return 5; }
        return if attempts == 3 { 7 } else { 4 };
    }
    if flags & 64 != 0 && flags & 112 != 112 { return 5; }
    if flags == 241 && failure == 0 { 2 } else { 3 }
}

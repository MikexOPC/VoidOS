use crate::plan::{build, ProcessPlan};
use crate::rules;
use core::slice;

// ====== x86_process_plan ======

/// # Safety
/// `bytes` must be readable for `length` bytes and `plan` writable for one `ProcessPlan`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_process_plan(bytes: *const u8, length: u64, plan: *mut ProcessPlan) -> u32 {
    if bytes.is_null() || plan.is_null() {
        return 200;
    }
    let image = unsafe { slice::from_raw_parts(bytes, length as usize) };
    let output = unsafe { &mut *plan };
    match build(image, output) {
        Ok(()) => 0,
        Err(code) => code,
    }
}

// ====== x86_process_copy_range ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_copy_range(pointer: u64, length: u64) -> u32 {
    rules::copy_range(pointer, length)
}

// ====== x86_process_return_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_return_valid(rip: u64, cs: u64, flags: u64, rsp: u64, ss: u64) -> u32 {
    u32::from(rules::return_valid(rip, cs, flags, rsp, ss))
}

// ====== x86_process_irq_frame_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_irq_frame_valid(rip: u64, cs: u64, flags: u64, rsp: u64, ss: u64) -> u32 {
    u32::from(rules::irq_frame_valid(rip, cs, flags, rsp, ss))
}

// ====== x86_process_fault_recoverable ======

#[unsafe(no_mangle)]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn x86_process_fault_recoverable(
    rip: u64,
    cs: u64,
    flags: u64,
    error: u64,
    cr2: u64,
    first: u64,
    last: u64,
    pointer: u64,
    length: u64,
    write: u64,
) -> u32 {
    u32::from(rules::fault_recoverable(rip, cs, flags, error, cr2, (first, last), (pointer, length), write != 0))
}

// ====== x86_process_nmi_valid ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_nmi_valid(window: u64, user_root: u32, kernel_gs: u32, ac: u32, cs: u64) -> u32 {
    u32::from(rules::nmi_valid(window, user_root != 0, kernel_gs != 0, ac != 0, cs))
}

// ====== x86_process_console_filter ======

/// # Safety
/// `bytes` must be writable for `length` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn x86_process_console_filter(bytes: *mut u8, length: u64) {
    if !bytes.is_null() {
        rules::filter_console(unsafe { slice::from_raw_parts_mut(bytes, length as usize) });
    }
}

// ====== x86_process_state_next ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_state_next(state: u32, next: u32) -> u32 {
    u32::from(rules::state_next(state, next))
}

// ====== x86_process_budget_initial ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_budget_initial() -> u64 {
    rules::BUDGET_TICKS
}

// ====== x86_process_budget_next ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_budget_next(remaining: u64) -> u64 {
    rules::budget_next(remaining)
}

// ====== x86_process_write_allowed ======

#[unsafe(no_mangle)]
pub extern "C" fn x86_process_write_allowed(total: u64, length: u64) -> u32 {
    u32::from(rules::write_allowed(total, length))
}

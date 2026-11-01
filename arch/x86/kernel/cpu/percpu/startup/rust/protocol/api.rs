#![cfg_attr(not(test), no_std)]
#![deny(unsafe_op_in_unsafe_fn)]
mod state;
#[path = "../record/entry.rs"]
mod record;
#[path = "../blob/layout.rs"]
mod blob;
#[path = "../return/plan.rs"]
mod return_plan;
static STARTUP: state::Startup = state::Startup::new();
#[cfg(not(target_has_atomic = "64"))]
compile_error!("AP startup requires native atomic64");

// ====== xs_configure ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_configure(count: u32) -> u32 { STARTUP.configure(count) }

// ====== xs_reserve ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_reserve(index: u32) -> u64 { STARTUP.reserve(index) }

// ====== xs_advance ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_advance(token: u64, phase: u64) -> u32 {
    if !(state::PREPARED..=state::PRIVATE).contains(&phase) { return 0; }
    STARTUP.advance(token, phase)
}

// ====== xs_cancel ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_cancel(token: u64) -> u32 { STARTUP.cancel(token) }

// ====== xs_quarantine ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_quarantine(token: u64) -> u32 { STARTUP.quarantine(token) }

// ====== xs_snapshot ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_snapshot() -> u64 { STARTUP.snapshot() }

// ====== xs_entry_claim ======

#[unsafe(no_mangle)]
pub extern "C" fn xs_entry_claim(entry: record::Entry, bits: u32, observed_id: u32) -> u32 {
    if observed_id != entry.apic_id || record::xs_entry_valid(entry, bits) == 0 { return 0; }
    STARTUP.advance(entry.token, state::ARMED)
}
